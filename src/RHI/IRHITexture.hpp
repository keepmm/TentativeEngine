/*****************************************************************//**
 * \file   IRHITexture.hpp
 * \brief  RHI テクスチャ
 * 
 * 作成者 keepmm
 * 作成日 2026/9/30
 * 更新履歴 9.30 作成
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

class IRHITexture : public IRHIObject
{
public:
	/// @brief 生成時のDesc
	virtual const RHITextureDesc& GetDesc() const noexcept = 0;

	/// @brief D3D12 : ID3D12Resource* / VK : VkImage
	virtual void* GetNativeResource() const noexcept = 0;
};
