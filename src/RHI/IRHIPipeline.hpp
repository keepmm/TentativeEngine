/*****************************************************************//**
 * \file   IRHIPipeline.hpp
 * \brief  RHI パイプラインレイアウト / パイプラインステート
 *         D3D12 : ID3D12RootSignature / ID3D12PipelineState
 *         Vulkan: VkPipelineLayout(+VkDescriptorSetLayout) / VkPipeline
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

/// @brief ルートシグネチャ相当。スロット番号 = RHIPipelineLayoutDesc::slots の添字
class IRHIPipelineLayout : public IRHIObject
{
public:
	virtual uint32_t GetSlotCount() const noexcept = 0;
	virtual ERHIBindingType GetSlotType(_In_ uint32_t slot) const noexcept = 0;

	/// @brief スロットの設定（RHIPipelineLayoutDesc::slots[slot] の写し）
	virtual const RHIBindingSlot& GetSlot(_In_ uint32_t slot) const noexcept = 0;

	/// @brief D3D12: ID3D12RootSignature* / VK: VkPipelineLayout
	virtual void* GetNativeLayout() const noexcept = 0;
};

/// @brief PSO。シェーダー・頂点レイアウト・ブレンドなどをまとめて焼き込んだもの
class IRHIPipelineState : public IRHIObject
{
public:
	virtual IRHIPipelineLayout* GetLayout() const noexcept = 0;

	/// @brief D3D12: ID3D12PipelineState* / VK: VkPipeline
	virtual void* GetNativePipeline() const noexcept = 0;
};
