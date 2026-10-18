/*****************************************************************//**
 * \file   D3D12DescriptorTable.hpp
 * \brief  IRHIDescriptorTable の D3D12 実装
 *         デバイスのシェーダー可視ヒープから連続した範囲を借りて、
 *         テクスチャの SRV（CPU 専用ヒープ上）をそこへコピーする
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "../IRHIDescriptorTable.hpp"

class D3D12Device;

class D3D12DescriptorTable final : public IRHIDescriptorTable
{
public:
	/// @param start シェーダー可視ヒープ上の先頭番号（D3D12Device が確保して渡す）
	D3D12DescriptorTable(
		_In_ D3D12Device& device,
		_In_ uint32_t start,
		_In_ uint32_t count);
	~D3D12DescriptorTable() override;

	// ---- IRHIDescriptorTable ---- //
	uint32_t GetCount() const noexcept override { return m_Count; }
	void SetTexture(_In_ uint32_t index, _In_ const RHITextureRef& texture) override;
	void ClearTexture(_In_ uint32_t index) override;

	// ---- D3D12 バックエンド内部用 ---- //

	/// @brief SetGraphicsRootDescriptorTable に渡す先頭の GPU ハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const noexcept;

	/// @brief index 番目の GPU ハンドル（1 枚だけ欲しいとき。ImGui::Image など）
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(_In_ uint32_t index) const noexcept;

private:
	D3D12Device&				m_Device;
	uint32_t					m_Start = 0;
	uint32_t					m_Count = 0;
	std::vector<RHITextureRef>	m_Textures;	// 置いたテクスチャを生かしておく
};
