/*****************************************************************//**
 * \file   D3D12Pipeline.hpp
 * \brief  IRHIPipelineLayout / IRHIPipelineState の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "../IRHIPipeline.hpp"

class D3D12PipelineLayout final : public IRHIPipelineLayout
{
public:
	/// @brief RHIPipelineLayoutDesc からルートシグネチャを作る
	/// @return 失敗時は nullptr
	static std::unique_ptr<D3D12PipelineLayout> Create(
		_In_ ID3D12Device* device,
		_In_ const RHIPipelineLayoutDesc& desc);

	D3D12PipelineLayout(
		_In_ ComPtr<ID3D12RootSignature> rootSignature,
		_In_ std::vector<RHIBindingSlot> slots)
		: m_RootSignature(std::move(rootSignature))
		, m_Slots(std::move(slots))
	{
	}

	uint32_t GetSlotCount() const noexcept override { return static_cast<uint32_t>(m_Slots.size()); }
	ERHIBindingType GetSlotType(_In_ uint32_t slot) const noexcept override { return m_Slots[slot].type; }
	const RHIBindingSlot& GetSlot(_In_ uint32_t slot) const noexcept override { return m_Slots[slot]; }
	void* GetNativeLayout() const noexcept override { return m_RootSignature.Get(); }

	ID3D12RootSignature* GetRootSignature() const noexcept { return m_RootSignature.Get(); }

private:
	ComPtr<ID3D12RootSignature>		m_RootSignature;
	std::vector<RHIBindingSlot>		m_Slots;	// テーブルの枚数とコマンドリスト側の確認に使う
};

class D3D12PipelineState final : public IRHIPipelineState
{
public:
	static constexpr uint32_t MAX_VERTEX_BUFFERS = 8;

	/// @brief RHIGraphicsPipelineDesc から PSO を作る
	/// @return 失敗時は nullptr（原因はデバッグレイヤーの出力に出る）
	static std::unique_ptr<D3D12PipelineState> Create(
		_In_ ID3D12Device* device,
		_In_ const RHIGraphicsPipelineDesc& desc);

	IRHIPipelineLayout* GetLayout() const noexcept override { return m_Layout.get(); }
	void* GetNativePipeline() const noexcept override { return m_Pipeline.Get(); }

	ID3D12PipelineState* GetPipelineState() const noexcept { return m_Pipeline.Get(); }
	ID3D12RootSignature* GetRootSignature() const noexcept
	{
		return static_cast<D3D12PipelineLayout*>(m_Layout.get())->GetRootSignature();
	}

	/// @brief IASetPrimitiveTopology に渡す値（PSO を設定するときに一緒に設定する）
	D3D_PRIMITIVE_TOPOLOGY GetTopology() const noexcept { return m_Topology; }

	/// @brief 頂点バッファのストライド。D3D12 は頂点バッファを設定するときに要る
	uint32_t GetVertexStride(_In_ uint32_t bufferSlot) const noexcept
	{
		return bufferSlot < m_VertexBufferCount ? m_VertexStrides[bufferSlot] : 0;
	}

private:
	ComPtr<ID3D12PipelineState>	m_Pipeline;
	RHIPipelineLayoutRef		m_Layout;	// レイアウトは PSO が生かしておく
	D3D_PRIMITIVE_TOPOLOGY		m_Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	uint32_t					m_VertexStrides[MAX_VERTEX_BUFFERS]{};
	uint32_t					m_VertexBufferCount = 0;
};
