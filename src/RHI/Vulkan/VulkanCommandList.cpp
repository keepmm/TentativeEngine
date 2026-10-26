#include "VulkanCommandList.hpp"
#include "VulkanResource.hpp"
#include "VulkanDescriptorTable.hpp"

#include <cassert>
#include <algorithm>

VulkanCommandList::VulkanCommandList(VkDevice device, VkCommandPool pool, VkCommandBuffer cmd) : m_Device(device), m_Pool(pool), m_Cmd(cmd)
{
	// ConstantBuffer / StructuredBuffer を一時的にバインドするためのプール
	// Begin() のたびに丸ごとリセットする
	VkDescriptorPoolSize sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256 },
		{ VK_DESCRIPTOR_TYPE_SAMPLER,        256 },
	};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 256;
	poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
	poolInfo.pPoolSizes = sizes;
	vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_TempDescPool);
}

VulkanCommandList::~VulkanCommandList()
{
	if (m_TempDescPool != VNULL)
	{
		vkDestroyDescriptorPool(m_Device, m_TempDescPool, nullptr);
		m_TempDescPool = VNULL;
	}
	if (m_Pool != VNULL)
	{
		// コマンドプールを破棄すると
		// そこに割り当てたCommandBufferも
		// 自動で破棄される
		vkDestroyCommandPool(m_Device, m_Pool, nullptr);
		m_Pool = VNULL;
		m_Cmd = VNULL;
	}
}

void VulkanCommandList::Begin()
{
	assert(!m_Recording && "Begin を 二回続けて呼んだ");

	// アロケータをリセットして
	// 最初から記録できるようにする
	vkResetCommandPool(m_Device, m_Pool, 0);

	// 前回の記録で一時的に割り当てたデスクリプタセットをまとめて返す
	if (m_TempDescPool != VNULL)
	{
		vkResetDescriptorPool(m_Device, m_TempDescPool, 0);
	}

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(m_Cmd, &beginInfo);
	m_Recording = true;
	m_InRenderPass = false;
	m_Pipeline = nullptr;
}

void VulkanCommandList::End()
{
	assert(m_Recording && "Begin してないのに Endを呼んだ");
	assert(!m_InRenderPass && "EndRenderPassを忘れている");

	vkEndCommandBuffer(m_Cmd);
	m_Recording = false;
}


// ---- バリア ---- //
void VulkanCommandList::Transition(IRHITexture* texture, ERHIResourceState before, ERHIResourceState after)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではバリアを張れない（Vulkan の制約）");
	if (texture == nullptr || before == after) return;

	auto* vkTex = static_cast<VulkanTexture*>(texture);
	const RHITextureDesc& td = vkTex->GetDesc();
	const bool isDepth = IsDepthFormat(td.format);

	const VkStateInfo src = ToVkStateInfo(before, isDepth);
	const VkStateInfo dst = ToVkStateInfo(after, isDepth);

	VkImageMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = src.stage;
	barrier.srcAccessMask = src.access;
	barrier.dstStageMask = dst.stage;
	barrier.dstAccessMask = dst.access;
	barrier.oldLayout = src.layout;
	barrier.newLayout = dst.layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = vkTex->GetImage();

	barrier.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = td.mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = (td.dimension == ERHITextureDimension::Texture3D) ? 1 : td.depthOrArraySize;

	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.imageMemoryBarrierCount = 1;
	depInfo.pImageMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(m_Cmd, &depInfo);
}

void VulkanCommandList::Transition(IRHIBuffer* buffer, ERHIResourceState before, ERHIResourceState after)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではバリアを張れない（Vulkan の制約）");
	if (buffer == nullptr) return;

	auto* vkBuf = static_cast<VulkanBuffer*>(buffer);
	const VkStateInfo src = ToVkStateInfo(before);
	const VkStateInfo dst = ToVkStateInfo(after);

	VkBufferMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	barrier.srcStageMask = src.stage;
	barrier.srcAccessMask = src.access;
	barrier.dstStageMask = dst.stage;
	barrier.dstAccessMask = dst.access;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = vkBuf->GetVkBuffer();
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.bufferMemoryBarrierCount = 1;
	depInfo.pBufferMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(m_Cmd, &depInfo);
}

// ---- レンダーパス (Dynamic Rendering) ---- //
void VulkanCommandList::BeginRenderPass(const RHIRenderPassDesc& desc)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "BeginRenderPass が入れ子になっている");
	assert(desc.numColors <= RHI_MAX_RENDER_TARGETS);

	uint32_t renderWidth = 0;
	uint32_t renderHeight = 0;

	// カラーアタッチメントの組み立て
	VkRenderingAttachmentInfo colorAttachments[RHI_MAX_RENDER_TARGETS]{};
	for (uint32_t i = 0; i < desc.numColors; ++i)
	{
		auto* tex = static_cast<VulkanTexture*>(desc.colors[i].texture);
		renderWidth = tex->GetDesc().width;
		renderHeight = tex->GetDesc().height;

		VkRenderingAttachmentInfo& att = colorAttachments[i];
		att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		att.imageView = tex->GetVkImageView();
		att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		att.loadOp = ToVkLoadOp(desc.colors[i].loadOp);
		att.storeOp = ToVkStoreOp(desc.colors[i].storeOp);
		for (int c = 0; c < 4; ++c)
		{
			att.clearValue.color.float32[c] = desc.colors[i].clearColor[c];
		}
	}

	// デプスアタッチメントの組み立て
	VkRenderingAttachmentInfo depthAttachment{};
	const bool hasDepth = (desc.depth.texture != nullptr);
	if (hasDepth)
	{
		auto* depthTex = static_cast<VulkanTexture*>(desc.depth.texture);
		if (renderWidth == 0)
		{
			renderWidth = depthTex->GetDesc().width;
			renderHeight = depthTex->GetDesc().height;
		}

		depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachment.imageView = depthTex->GetVkImageView();
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = ToVkLoadOp(desc.depth.loadOp);
		depthAttachment.storeOp = ToVkStoreOp(desc.depth.storeOp);
		depthAttachment.clearValue.depthStencil.depth = desc.depth.clearDepth;
		depthAttachment.clearValue.depthStencil.stencil = desc.depth.clearStencil;
	}

	VkRenderingInfo renderInfo{};
	renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderInfo.renderArea.offset = { 0, 0 };
	renderInfo.renderArea.extent = { renderWidth, renderHeight };
	renderInfo.layerCount = 1;
	renderInfo.colorAttachmentCount = desc.numColors;
	renderInfo.pColorAttachments = colorAttachments;
	renderInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

	vkCmdBeginRendering(m_Cmd, &renderInfo);
	m_InRenderPass = true;
}

void VulkanCommandList::EndRenderPass()
{
	assert(m_InRenderPass && "BeginRenderPass していない");

	vkCmdEndRendering(m_Cmd);
	m_InRenderPass = false;
}

void VulkanCommandList::SetViewport(const RHIViewport& viewport)
{
	// Vulkan は Y 軸が下向きなので、y を下端にずらして height をマイナスにすることで D3D12 と同じ座標系にする
	VkViewport vp{};
	vp.x = viewport.x;
	vp.y = viewport.y + viewport.height;
	vp.width = viewport.width;
	vp.height = -viewport.height;
	vp.minDepth = viewport.minDepth;
	vp.maxDepth = viewport.maxDepth;

	vkCmdSetViewport(m_Cmd, 0, 1, &vp);
}

void VulkanCommandList::SetScissor(const RHIRect& rect)
{
	VkRect2D r{};
	r.offset.x = (std::max)(0, rect.left);
	r.offset.y = (std::max)(0, rect.top);
	r.extent.width = static_cast<uint32_t>((std::max)(0, rect.right - rect.left));
	r.extent.height = static_cast<uint32_t>((std::max)(0, rect.bottom - rect.top));

	vkCmdSetScissor(m_Cmd, 0, 1, &r);
}

// ---- コピー ---- //
void VulkanCommandList::CopyBuffer(
	IRHIBuffer* dst, uint64_t dstOffset,
	IRHIBuffer* src, uint64_t srcOffset,
	uint64_t size)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではコピーできない（Vulkan の制約）");

	VkBufferCopy region{};
	region.srcOffset = srcOffset;
	region.dstOffset = dstOffset;
	region.size = size;

	vkCmdCopyBuffer(
		m_Cmd,
		static_cast<VulkanBuffer*>(src)->GetVkBuffer(),
		static_cast<VulkanBuffer*>(dst)->GetVkBuffer(),
		1, &region);
}

void VulkanCommandList::CopyBufferToTexture(
	IRHITexture* dst, uint32_t dstSubresource,
	IRHIBuffer* src, const RHISubresourceFootprint& footprint)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではコピーできない（Vulkan の制約）");

	auto* vkTex = static_cast<VulkanTexture*>(dst);
	const RHITextureDesc& td = vkTex->GetDesc();
	const uint32_t bpp = GetFormatByteSize(td.format);
	const uint32_t blockSize = IsCompressedFormat(td.format) ? 4u : 1u;

	const uint32_t mip = dstSubresource % td.mipLevels;
	const uint32_t slice = dstSubresource / td.mipLevels;

	VkBufferImageCopy region{};
	region.bufferOffset = footprint.offset;
	// Vulkan の bufferRowLength は「バイト数」ではなく「ピクセル（テクセル）数」で指定する
	region.bufferRowLength = (footprint.rowPitch / bpp) * blockSize;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = IsDepthFormat(td.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = mip;
	region.imageSubresource.baseArrayLayer = slice;
	region.imageSubresource.layerCount = 1;

	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { footprint.width, footprint.height, footprint.depth };

	vkCmdCopyBufferToImage(
		m_Cmd,
		static_cast<VulkanBuffer*>(src)->GetVkBuffer(),
		vkTex->GetImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &region);
}

void VulkanCommandList::CopyTextureToBuffer(
	IRHIBuffer* dst, const RHISubresourceFootprint& footprint,
	IRHITexture* src, uint32_t srcSubresource)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではコピーできない（Vulkan の制約）");

	auto* vkTex = static_cast<VulkanTexture*>(src);
	const RHITextureDesc& td = vkTex->GetDesc();
	const uint32_t bpp = GetFormatByteSize(td.format);
	const uint32_t blockSize = IsCompressedFormat(td.format) ? 4u : 1u;

	const uint32_t mip = srcSubresource % td.mipLevels;
	const uint32_t slice = srcSubresource / td.mipLevels;

	VkBufferImageCopy region{};
	region.bufferOffset = footprint.offset;
	region.bufferRowLength = (footprint.rowPitch / bpp) * blockSize;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = IsDepthFormat(td.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = mip;
	region.imageSubresource.baseArrayLayer = slice;
	region.imageSubresource.layerCount = 1;

	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { footprint.width, footprint.height, footprint.depth };

	vkCmdCopyImageToBuffer(
		m_Cmd,
		vkTex->GetImage(),
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		static_cast<VulkanBuffer*>(dst)->GetVkBuffer(),
		1, &region);
}
void VulkanCommandList::CheckSlot(uint32_t slot, ERHIBindingType type) const
{
	assert(m_Pipeline != nullptr && "SetPipelineState より前にバインドしている");
	assert(slot < m_Pipeline->GetLayout()->GetSlotCount() && "レイアウトに無いスロット");
	assert(m_Pipeline->GetLayout()->GetSlotType(slot) == type && "スロットの種類が違う");
	(void)slot;
	(void)type;
}
// ---- パイプライン / バインド ---- //
void VulkanCommandList::SetPipelineState(IRHIPipelineState* pipeline)
{ 
	assert(m_Recording);
	m_Pipeline = static_cast<VulkanPipelineState*>(pipeline);

	vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetVkPipeline());;
}
void VulkanCommandList::SetConstantBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
	CheckSlot(slot, ERHIBindingType::ConstantBuffer);

	auto* vkLayout = static_cast<VulkanPipelineLayout*>(m_Pipeline->GetLayout());
	VkDescriptorSetLayout setLayout = vkLayout->GetVkSetLayout(slot);

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_TempDescPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &setLayout;

	VkDescriptorSet set = VNULL;
	if (vkAllocateDescriptorSets(m_Device, &allocInfo, &set) != VK_SUCCESS)return;

	auto* vkBuf = static_cast<VulkanBuffer*>(buffer);
	VkDescriptorBufferInfo bufInfo{};
	bufInfo.buffer = vkBuf->GetVkBuffer();
	bufInfo.offset = offset;
	bufInfo.range = vkBuf->GetDesc().size - offset;

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = vkLayout->GetSlot(slot).shaderRegister;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write.pBufferInfo = &bufInfo;

	vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	vkCmdBindDescriptorSets(
		m_Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_Pipeline->GetVkPipelineLayout(), slot, 1, &set, 0, nullptr);
}
void VulkanCommandList::SetStructuredBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
	CheckSlot(slot, ERHIBindingType::StructuredBuffer);
	auto* vkLayout = static_cast<VulkanPipelineLayout*>(m_Pipeline->GetLayout());
	VkDescriptorSetLayout setLayout = vkLayout->GetVkSetLayout(slot);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_TempDescPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &setLayout;
	VkDescriptorSet set = VNULL;
	if (vkAllocateDescriptorSets(m_Device, &allocInfo, &set) != VK_SUCCESS) return;
	auto* vkBuf = static_cast<VulkanBuffer*>(buffer);
	VkDescriptorBufferInfo bufInfo{};
	bufInfo.buffer = vkBuf->GetVkBuffer();
	bufInfo.offset = offset;
	bufInfo.range = vkBuf->GetDesc().size - offset;
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = vkLayout->GetSlot(slot).shaderRegister;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.pBufferInfo = &bufInfo;
	vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	vkCmdBindDescriptorSets(
		m_Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_Pipeline->GetVkPipelineLayout(), slot, 1, &set, 0, nullptr);
}
void VulkanCommandList::SetVertexBuffer(uint32_t bufferSlot, IRHIBuffer* buffer, uint64_t offset)
{
	VkBuffer vkBuf = static_cast<VulkanBuffer*>(buffer)->GetVkBuffer();
	VkDeviceSize vkOffset = offset;
	vkCmdBindVertexBuffers(m_Cmd, bufferSlot, 1, &vkBuf, &vkOffset);
}
void VulkanCommandList::SetIndexBuffer(IRHIBuffer* buffer, ERHIFormat format, uint64_t offset)
{
	assert(format == ERHIFormat::R16_UINT || format == ERHIFormat::R32_UINT);
	const VkIndexType indexType = (format == ERHIFormat::R16_UINT)
		? VK_INDEX_TYPE_UINT16
		: VK_INDEX_TYPE_UINT32;
	vkCmdBindIndexBuffer(
		m_Cmd,
		static_cast<VulkanBuffer*>(buffer)->GetVkBuffer(),
		offset,
		indexType);
}
void VulkanCommandList::SetDescriptorTable(uint32_t slot, IRHIDescriptorTable* table)
{
	CheckSlot(slot, ERHIBindingType::TextureTable);
	assert(table->GetCount() == m_Pipeline->GetLayout()->GetSlot(slot).count && "テーブルの枚数がスロットと違う");
	VkDescriptorSet set = static_cast<VulkanDescriptorTable*>(table)->GetVkDescriptorSet();
	vkCmdBindDescriptorSets(
		m_Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_Pipeline->GetVkPipelineLayout(), slot, 1, &set, 0, nullptr);
}
// ---- 描画 ---- //
void VulkanCommandList::Draw(
	uint32_t vertexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstInstance)
{
	assert(m_InRenderPass && "Draw はレンダーパスの中で");
	assert(m_Pipeline != nullptr && "SetPipelineState していない");
	vkCmdDraw(m_Cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}
void VulkanCommandList::DrawIndexed(
	uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstIndex, int32_t baseVertex,
	uint32_t firstInstance)
{
	assert(m_InRenderPass && "DrawIndexed はレンダーパスの中で");
	assert(m_Pipeline != nullptr && "SetPipelineState していない");
	vkCmdDrawIndexed(m_Cmd, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}