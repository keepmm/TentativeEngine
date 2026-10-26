/*****************************************************************//**
 * \file   VulkanCommandList.hpp
 * \brief  IRHICommandList の Vulkan 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/19
 * 更新履歴 10.19 作成
 * *********************************************************************/
#pragma once

#include "VulkanTypes.hpp"
#include "../IRHICommandList.hpp"
#include "VulkanPipeline.hpp"

class VulkanCommandList final : public IRHICommandList
{
public:
	VulkanCommandList(
		_In_ VkDevice device,
		_In_ VkCommandPool pool,
		_In_ VkCommandBuffer cmd);
	~VulkanCommandList() override;

	// ---- IRHICommandList : 記録 ---- //
	void Begin() override;
	void End() override;

	// ---- IRHICommandList : バリア ---- //
	void Transition(_In_ IRHITexture* texture, _In_ ERHIResourceState before, _In_ ERHIResourceState after) override;
	void Transition(_In_ IRHIBuffer* buffer, _In_ ERHIResourceState before, _In_ ERHIResourceState after) override;

	// ---- IRHICommandList : レンダーパス ---- //
	void BeginRenderPass(_In_ const RHIRenderPassDesc& desc) override;
	void EndRenderPass() override;
	void SetViewport(_In_ const RHIViewport& viewport) override;
	void SetScissor(_In_ const RHIRect& rect) override;

	// ---- IRHICommandList : パイプライン / バインド ---- //
	void SetPipelineState(_In_ IRHIPipelineState* pipeline) override;
	void SetConstantBuffer(_In_ uint32_t slot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset) override;
	void SetStructuredBuffer(_In_ uint32_t slot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset) override;
	void SetVertexBuffer(_In_ uint32_t bufferSlot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset) override;
	void SetIndexBuffer(_In_ IRHIBuffer* buffer, _In_ ERHIFormat format, _In_ uint64_t offset) override;
	void SetDescriptorTable(_In_ uint32_t slot, _In_ IRHIDescriptorTable* table) override;

	// ---- IRHICommandList : 描画 ---- //
	void Draw(
		_In_ uint32_t vertexCount, _In_ uint32_t instanceCount,
		_In_ uint32_t firstVertex, _In_ uint32_t firstInstance) override;
	void DrawIndexed(
		_In_ uint32_t indexCount, _In_ uint32_t instanceCount,
		_In_ uint32_t firstIndex, _In_ int32_t baseVertex,
		_In_ uint32_t firstInstance) override;

	// ---- IRHICommandList : コピー ---- //
	void CopyBuffer(
		_In_ IRHIBuffer* dst, _In_ uint64_t dstOffset,
		_In_ IRHIBuffer* src, _In_ uint64_t srcOffset,
		_In_ uint64_t size) override;
	void CopyBufferToTexture(
		_In_ IRHITexture* dst, _In_ uint32_t dstSubresource,
		_In_ IRHIBuffer* src, _In_ const RHISubresourceFootprint& footprint) override;
	void CopyTextureToBuffer(
		_In_ IRHIBuffer* dst, _In_ const RHISubresourceFootprint& footprint,
		_In_ IRHITexture* src, _In_ uint32_t srcSubresource) override;

	void* GetNativeCommandList() const noexcept override { return m_Cmd; }

	// ---- Vulkan バックエンド内部用 ---- //
	VkCommandBuffer GetVkCommandBuffer() const noexcept { return m_Cmd; }
	bool IsClosed() const noexcept { return !m_Recording; }

private:
	void CheckSlot(_In_ uint32_t slot, _In_ ERHIBindingType type) const;

	VkDevice		m_Device = VK_NULL_HANDLE;
	VkCommandPool	m_Pool = VK_NULL_HANDLE;
	VkCommandBuffer	m_Cmd = VK_NULL_HANDLE;

	VkDescriptorPool m_TempDescPool = VNULL;

	bool m_Recording = false;
	bool m_InRenderPass = false;

	VulkanPipelineState* m_Pipeline = nullptr;
};