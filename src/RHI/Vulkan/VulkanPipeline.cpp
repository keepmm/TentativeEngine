#include "VulkanPipeline.hpp"
#include "Core/Log.hpp"
#include <cassert>

std::unique_ptr<VulkanPipelineLayout> VulkanPipelineLayout::Create(VkDevice device, const RHIPipelineLayoutDesc& desc)
{
	std::vector<VkSampler> samplers;
	samplers.reserve(desc.staticSamplers.size());

	// 静的サンプラーの作成
	for (const ERHIStaticSamplerDesc& sd : desc.staticSamplers)
	{
		VkSamplerCreateInfo samplerInfo{};

		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = (sd.filter == ERHIFilter::Point) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
		samplerInfo.minFilter = (sd.filter == ERHIFilter::Point) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = (sd.filter == ERHIFilter::Point) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = ToVkAddressMode(sd.address);
		samplerInfo.addressModeV = ToVkAddressMode(sd.address);
		samplerInfo.addressModeW = ToVkAddressMode(sd.address);
		samplerInfo.anisotropyEnable = (sd.filter == ERHIFilter::Anisotropic) ? VK_TRUE : VK_FALSE;
		samplerInfo.maxAnisotropy = static_cast<float>(sd.maxAnisotropy);
		samplerInfo.compareEnable = sd.compareEnable ? VK_TRUE : VK_FALSE;
		samplerInfo.compareOp = ToVkCompareOp(sd.compareFunc);
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.borderColor = ToVkBorderColor(sd.borderColor);

		VkSampler sampler = VNULL;
		if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
		{
			return nullptr;
		}
		samplers.push_back(sampler);
	}

	// 各スロットごとにVkDescriptorSetlayOutを作成
	std::vector<VkDescriptorSetLayout> setLayouts;
	setLayouts.reserve(desc.slots.size());

	for (size_t i = 0; i < desc.slots.size(); ++i)
	{
		const RHIBindingSlot& slot = desc.slots[i];
		std::vector<VkDescriptorSetLayoutBinding> bindings;

		// 定数バッファ
		if (slot.type == ERHIBindingType::ConstantBuffer)
		{
			VkDescriptorSetLayoutBinding b{};
			b.binding = slot.shaderRegister;
			b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			b.descriptorCount = 1;
			b.stageFlags = ToVkShaderStageFlags(slot.visibility);
			bindings.push_back(b);
		}
		else if (slot.type == ERHIBindingType::StructuredBuffer)
		{
			VkDescriptorSetLayoutBinding b{};
			b.binding = slot.shaderRegister;
			b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			b.descriptorCount = slot.count;
			b.stageFlags = ToVkShaderStageFlags(slot.visibility);
			bindings.push_back(b);
		}
		else if (slot.type == ERHIBindingType::TextureTable)
		{
			for (uint32_t t = 0; t < slot.count; ++t)
			{
				VkDescriptorSetLayoutBinding b{};
				b.binding = slot.shaderRegister + t;
				b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				b.descriptorCount = 1;
				b.stageFlags = ToVkShaderStageFlags(slot.visibility);
				bindings.push_back(b);
			}
		}

		// 先頭スロットに静的サンプラーも同梱する
		if (i == 0)
		{
			for (size_t s = 0; s < desc.staticSamplers.size(); ++s)
			{
				VkDescriptorSetLayoutBinding sb{};
				sb.binding = 100 + desc.staticSamplers[s].shaderRegister;
				sb.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				sb.descriptorCount = 1;
				sb.stageFlags = ToVkShaderStageFlags(desc.staticSamplers[s].visibility);
				sb.pImmutableSamplers = &samplers[s];
				bindings.push_back(sb);
			}
		}

			VkDescriptorSetLayoutCreateInfo setInfo{};
			setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
			setInfo.pBindings = bindings.data();
			VkDescriptorSetLayout setLayout = VNULL;
			if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout) != VK_SUCCESS)
			{
				return nullptr;
			}
			setLayouts.push_back(setLayout);
		}

		// VkPipelineLayoutの作成
	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	layoutInfo.pSetLayouts = setLayouts.data();

	VkPipelineLayout pipelineLayout = VNULL;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
	{
		return nullptr;
	}

	return std::make_unique<VulkanPipelineLayout>(
			device,pipelineLayout,std::move(setLayouts),std::move(samplers),desc.slots);
}

VulkanPipelineLayout::VulkanPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, std::vector<VkDescriptorSetLayout> setLayouts, std::vector<VkSampler> immutableSamplers, std::vector<RHIBindingSlot> slots)
	: m_Device(device)
	, m_PipelineLayout(pipelineLayout)
	, m_SetLayouts(std::move(setLayouts))
	, m_ImmutableSamplers(std::move(immutableSamplers))
	, m_Slots(std::move(slots))
{}

VulkanPipelineLayout::~VulkanPipelineLayout()
{
	if (m_PipelineLayout != VNULL)
	{
		vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
		m_PipelineLayout = VNULL;
	}
	for (VkDescriptorSetLayout setLayout : m_SetLayouts)
	{
		if (setLayout != VNULL)
		{
			vkDestroyDescriptorSetLayout(m_Device, setLayout, nullptr);
		}
	}
	for (VkSampler sampler : m_ImmutableSamplers)
	{
		if (sampler != VNULL)
		{
			vkDestroySampler(m_Device, sampler, nullptr);
		}
	}
}

VulkanPipelineState::VulkanPipelineState(
	VkDevice device,
	VkPipeline pipeline,
	RHIPipelineLayoutRef layout)
	: m_Device(device)
	, m_Pipeline(pipeline)
	, m_Layout(std::move(layout))
{}

VulkanPipelineState::~VulkanPipelineState()
{
	if (m_Pipeline != VNULL)
	{
		vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
		m_Pipeline = VNULL;
	}
}

std::unique_ptr<VulkanPipelineState> VulkanPipelineState::Create(VkDevice device, const RHIGraphicsPipelineDesc& desc)
{
	if (!desc.layout || !desc.vs.IsValid()) return nullptr;

	// シェーダーモジュールの作成
	auto createModule = [&](const RHIShaderByteCode& code) -> VkShaderModule
		{
			VkShaderModuleCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = code.size;
			info.pCode = static_cast<const uint32_t*>(code.data);

			VkShaderModule mod = VNULL;
			vkCreateShaderModule(device, &info, nullptr, &mod);
			return mod;
		};

	VkShaderModule vsMod = createModule(desc.vs);
	VkShaderModule psMod = desc.ps.IsValid() ? createModule(desc.ps) : VNULL;
	if (vsMod == VNULL || (desc.ps.IsValid() && psMod == VNULL))
	{
		if (vsMod != VNULL) vkDestroyShaderModule(device, vsMod, nullptr);
		if (psMod != VNULL)vkDestroyShaderModule(device, vsMod, nullptr);
		return nullptr;
	}

	std::vector<VkPipelineShaderStageCreateInfo> stages;
	VkPipelineShaderStageCreateInfo vsStage{};
	vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vsStage.module = vsMod;
	vsStage.pName = desc.vs.entrypoint ? desc.vs.entrypoint : "main";
	stages.push_back(vsStage);

	if (psMod != VNULL)
	{
		VkPipelineShaderStageCreateInfo psStage{};
		psStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		psStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		psStage.module = psMod;
		psStage.pName = desc.ps.entrypoint ? desc.ps.entrypoint : "main";
		stages.push_back(psStage);
	}

	// 頂点入力レイアウト
	std::vector<VkVertexInputBindingDescription> vertexBindings(desc.vertexBuffers.size());
	for (size_t i = 0; i < desc.vertexBuffers.size(); ++i)
	{
		vertexBindings[i].binding = static_cast<uint32_t>(i);
		vertexBindings[i].stride = desc.vertexBuffers[i].stride;
		vertexBindings[i].inputRate = desc.vertexBuffers[i].perInstance
			? VK_VERTEX_INPUT_RATE_INSTANCE
			: VK_VERTEX_INPUT_RATE_VERTEX;
	}

	std::vector<VkVertexInputAttributeDescription> vertexAttrs(desc.vertexAttributes.size());
	for (size_t i = 0; i < desc.vertexAttributes.size(); ++i)
	{
		vertexAttrs[i].location = static_cast<uint32_t>(i);
		vertexAttrs[i].binding = desc.vertexAttributes[i].bufferSlot;
		vertexAttrs[i].format = ToVkFormat(desc.vertexAttributes[i].format);
		vertexAttrs[i].offset = desc.vertexAttributes[i].offset;
	}

	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
	vertexInput.pVertexBindingDescriptions = vertexBindings.data();
	vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttrs.size());
	vertexInput.pVertexAttributeDescriptions = vertexAttrs.data();

	// プリミティブトポロジー
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = ToVkTopology(desc.topology);

	// ビューポート
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// ラスタライザ
	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = desc.rasterizer.depthClipEnable ? VK_FALSE : VK_TRUE;
	rasterizer.polygonMode = ToVkPolygonMode(desc.rasterizer.fillMode);
	rasterizer.cullMode = ToVkCullMode(desc.rasterizer.cullMode);
	rasterizer.frontFace = desc.rasterizer.frontCounterClockwise
		? VK_FRONT_FACE_COUNTER_CLOCKWISE
		: VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = (desc.rasterizer.depthBias != 0 || desc.rasterizer.slopeScaledDepthBias != 0.0f) ? VK_TRUE : VK_FALSE;
	rasterizer.depthBiasConstantFactor = static_cast<float>(desc.rasterizer.depthBias);
	rasterizer.depthBiasClamp = desc.rasterizer.depthBiasClamp;
	rasterizer.depthBiasSlopeFactor = desc.rasterizer.slopeScaledDepthBias;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample{};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// 深度ステンシル
	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = ToVkCompareOp(desc.depthStencil.depthFunc);

	// ブレンド
	VkPipelineColorBlendAttachmentState blendAttachments[RHI_MAX_RENDER_TARGETS]{};
	for (uint32_t i = 0; i < desc.numRenderTargets; ++i)
	{
		const RHIBlendDesc& bd = desc.blend[i];
		VkPipelineColorBlendAttachmentState& att = blendAttachments[i];
		att.blendEnable = bd.blendEnable ? VK_TRUE : VK_FALSE;
		att.srcColorBlendFactor = ToVkBlendFactor(bd.srcColor);
		att.dstColorBlendFactor = ToVkBlendFactor(bd.dstColor);
		att.colorBlendOp = ToVkBlendOp(bd.colorOp);
		att.srcAlphaBlendFactor = ToVkBlendFactor(bd.srcAlpha);
		att.dstAlphaBlendFactor = ToVkBlendFactor(bd.dstAlpha);
		att.alphaBlendOp = ToVkBlendOp(bd.alphaOp);
		att.colorWriteMask = static_cast<VkColorComponentFlags>(bd.writeMask);
	}
	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = desc.numRenderTargets;
	colorBlending.pAttachments = blendAttachments;

	// 動的ステート (Viewport と Scissor)
	const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
	dynamicState.pDynamicStates = dynamicStates;

	// Dynamic Rendering 用のフォーマット指定
	VkFormat colorFormats[RHI_MAX_RENDER_TARGETS]{};
	for (uint32_t i = 0; i < desc.numRenderTargets; ++i)
	{
		colorFormats[i] = ToVkFormat(desc.rtvFormat[i]);
	}
	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = desc.numRenderTargets;
	renderingInfo.pColorAttachmentFormats = colorFormats;
	renderingInfo.depthAttachmentFormat = ToVkFormat(desc.dsvFormat);
	// VkPipeline の生成
	auto* vkLayout = static_cast<VulkanPipelineLayout*>(desc.layout.get());
	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineInfo.pStages = stages.data();
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = vkLayout->GetVkPipelineLayout();
	VkPipeline pipeline = VNULL;
	const VkResult res = vkCreateGraphicsPipelines(device, VNULL, 1, &pipelineInfo, nullptr, &pipeline);
	
	// ShaderModule は PSO を作り終えたらその場で破棄してよい
	vkDestroyShaderModule(device, vsMod, nullptr);
	if (psMod != VNULL) vkDestroyShaderModule(device, psMod, nullptr);
	if (res != VK_SUCCESS)
	{
		LogError("[RHI] vkCreateGraphicsPipelines に失敗");
		return nullptr;
	}
	return std::make_unique<VulkanPipelineState>(device, pipeline, desc.layout);
}
