/*****************************************************************//**
 * \file   D3D12CommandList.hpp
 * \brief  IRHICommandList の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成（バリア / レンダーパス / コピー / PSO / 描画）
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "D3D12Pipeline.hpp"
#include "../IRHICommandList.hpp"

class D3D12CommandList final : public IRHICommandList
{
public:
	/// @param shaderVisibleHeap デバイスのシェーダー可視ヒープ（記録の最初に設定する）
	D3D12CommandList(
		_In_ ComPtr<ID3D12CommandAllocator> allocator,
		_In_ ComPtr<ID3D12GraphicsCommandList> list,
		_In_ ID3D12DescriptorHeap* shaderVisibleHeap);

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

	void* GetNativeCommandList() const noexcept override { return m_List.Get(); }

	// ---- D3D12 バックエンド内部用 ---- //
	ID3D12GraphicsCommandList* GetD3D12CommandList() const noexcept { return m_List.Get(); }

	/// @brief End 済み（= Submit できる）か
	bool IsClosed() const noexcept { return !m_Recording; }

private:
	static constexpr uint32_t MAX_VERTEX_BUFFERS = D3D12PipelineState::MAX_VERTEX_BUFFERS;

	void AddTransition(_In_ ID3D12Resource* resource, _In_ ERHIResourceState before, _In_ ERHIResourceState after);

	/// @brief 溜めたバリアを 1 回の ResourceBarrier で出す
	void FlushBarriers();

	/// @brief 頂点バッファを今の PSO のストライドで設定する（Draw の直前に呼ぶ）
	void FlushVertexBuffers();

	/// @brief デバッグ用: スロット番号と種類が今のレイアウトに合っているか
	void CheckSlot(_In_ uint32_t slot, _In_ ERHIBindingType type) const;

	ComPtr<ID3D12CommandAllocator>		m_Allocator;
	ComPtr<ID3D12GraphicsCommandList>	m_List;
	ID3D12DescriptorHeap*				m_ShaderVisibleHeap = nullptr;	// 持ち主はデバイス

	std::vector<D3D12_RESOURCE_BARRIER> m_PendingBarriers;
	bool m_Recording	= false;
	bool m_InRenderPass	= false;

	// ---- 今設定されているもの ---- //
	D3D12PipelineState*		m_Pipeline		= nullptr;
	ID3D12RootSignature*	m_RootSignature	= nullptr;

	struct VertexBufferBinding
	{
		IRHIBuffer*	buffer = nullptr;
		uint64_t	offset = 0;
	};
	VertexBufferBinding	m_VertexBuffers[MAX_VERTEX_BUFFERS]{};
	uint32_t			m_VertexBufferCount		= 0;	// 設定された最大スロット + 1
	bool				m_VertexBuffersDirty	= false;
};
