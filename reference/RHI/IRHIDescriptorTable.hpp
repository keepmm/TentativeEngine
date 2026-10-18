/*****************************************************************//**
 * \file   IRHIDescriptorTable.hpp
 * \brief  RHI デスクリプタテーブル（TextureTable スロットに結び付けるテクスチャの組）
 *         D3D12: シェーダー可視ヒープの連続範囲 / Vulkan: VkDescriptorSet
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

/// @brief IRHIDevice::CreateDescriptorTable(layout, slot) で作る。
///        枚数はそのスロットの count、index 0 がシェーダーの t(shaderRegister) に当たる
/// @note  GPU がこのテーブルを使っている間は書き換えない（D3D12 / Vulkan とも未定義動作）。
///        描画に使った後で中身を変えたいときは、新しいテーブルを作って差し替える
///        （古いテーブルは手放せば、GPU が使い終わってから遅延解放される）
class IRHIDescriptorTable : public IRHIObject
{
public:
	virtual uint32_t GetCount() const noexcept = 0;

	/// @brief index 番目にテクスチャを置く。テーブルが参照を持つので、置いている間は解放されない
	/// @note  テクスチャは ShaderResource 用途で作ったもの
	virtual void SetTexture(_In_ uint32_t index, _In_ const RHITextureRef& texture) = 0;

	/// @brief index 番目を空にする（シェーダーからは 0 が読める）
	virtual void ClearTexture(_In_ uint32_t index) = 0;
};
