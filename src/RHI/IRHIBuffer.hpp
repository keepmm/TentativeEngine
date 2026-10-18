/*****************************************************************//**
 * \file   IRHIBuffer.hpp
 * \brief  RHI バッファ
 * 
 * 作成者 keepmm
 * 作成日 2026/9/30
 * 更新履歴 9.30 作成
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

class IRHIBuffer : public IRHIObject
{
public:
	/// @brief 生成時のDesc
	/// @brief 定数バッファは 256
	virtual const RHIBufferDesc& GetDesc() const noexcept = 0;

	/// @brief CPUから触るためのポインタ
	/// @brief Upload : 生成時から常にマップ済み Unmap不要
	/// @brief ReadBack : 呼ぶたびにマップする GPUの書き込み完了(フェンス)を待ってから呼ぶ
	/// @brief GpuOnly : nullptr
	virtual void* Map() = 0;

	virtual void Unmap() = 0;

	/// @brief D3D12 : ID3D12Resource* 
	/// @brief    VK : VkBuffer
	virtual void* GetNativeResource() const noexcept = 0;
};
