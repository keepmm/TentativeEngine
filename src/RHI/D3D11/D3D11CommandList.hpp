/*****************************************************************//**
 * \file   D3D11CommandList.hpp
 * \brief  IRHICommandList の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include "D3D11Types.hpp"
#include "D3D11Resource.hpp"
#include "D3D11Pipeline.hpp"
#include "D3D11DescriptorTable.hpp"
#include "../IRHICommandList.hpp"

class D3D11CommandList final : public IRHICommandList
{
public:
	explicit D3D11CommandList(_In_ ComPtr<ID3D11DeviceContext> context);
	~D3D11CommandList() override = default;

	// ---- 記録の開始 / 終了 ---- //
	void Begin() override;
	void End() override;

	void Transition(_In_ IRHITexture* texture, _In_ ERHIResourceState before, _In_ ERHIResourceState after) override;
	void Transition(_In_ IRHIBuffer* buffer, _In_ ERHIResourceState before, _In_ ERHIResourceState after) override;

	// ---- レンダーパス ---- //
	void BeginRenderPass(_In_ const RHIRenderPassDesc& desc) override;
	void EndRenderPass() override;

	void SetViewport(_In_ const RHIViewport& viewport) override;
	void SetScissor(_In_ const RHIRect& rect) override;

	// ---- コピー ---- //
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

	// ---- パイプライン / バインド ---- //
	void SetPipelineState(_In_ IRHIPipelineState* pipeline) override;
	void SetConstantBuffer(_In_ uint32_t slot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset = 0) override;
	void SetStructuredBuffer(_In_ uint32_t slot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset = 0) override;
	void SetVertexBuffer(_In_ uint32_t bufferSlot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset = 0) override;
	void SetIndexBuffer(_In_ IRHIBuffer* buffer, _In_ ERHIFormat format, _In_ uint64_t offset = 0) override;
	void SetDescriptorTable(_In_ uint32_t slot, _In_ IRHIDescriptorTable* table) override;

	// ---- 描画 ---- //
	void Draw(
		_In_ uint32_t vertexCount, _In_ uint32_t instanceCount = 1,
		_In_ uint32_t firstVertex = 0, _In_ uint32_t firstInstance = 0) override;

	void DrawIndexed(
		_In_ uint32_t indexCount, _In_ uint32_t instanceCount = 1,
		_In_ uint32_t firstIndex = 0, _In_ int32_t baseVertex = 0,
		_In_ uint32_t firstInstance = 0) override;

	void* GetNativeCommandList() const noexcept override { return m_Context.Get(); }

private:
	ComPtr<ID3D11DeviceContext> m_Context;
	D3D11PipelineState* m_CurrentPso = nullptr;
};
