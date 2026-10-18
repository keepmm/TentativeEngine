/*****************************************************************//**
 * \file   IRHIFence.hpp
 * \brief  RHI フェンス(GPUの進み具合を数値で表す)
 * 
 * 作成者 
 * 作成日 2026/10/14
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

class IRHIFence : public IRHIObject
{
public:
	/// @brief GPUが到達した値
	virtual uint64_t GetCompletedValue() const = 0;

	/// @brief GPUふぁ value に届くまで CPUを止める
	virtual void Wait(_In_ uint64_t value) = 0;

	virtual void* GetNativeFence() const noexcept = 0;
};

