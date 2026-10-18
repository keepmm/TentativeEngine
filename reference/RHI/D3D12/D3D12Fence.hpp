/*****************************************************************//**
 * \file   D3D12Fence.hpp
 * \brief  IRHIFenceの D3D12 実装
 * 
 * 作成者 keeep
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "../IRHIFence.hpp"

class D3D12Fence : public IRHIFence
{
public:
	explicit D3D12Fence(_In_ ComPtr<ID3D12Fence> fence)
		: m_Fence(std::move(fence))
	{}

	uint64_t GetCompletedValue() const override { return m_Fence->GetCompletedValue(); }

	void Wait(_In_ uint64_t value)override
	{
		// デバイスロスト時は UINT64_MAXが変えるので、ここで抜けて固まらない
		if (m_Fence->GetCompletedValue() >= value)return;

		// イベントにnullptrを渡すと 届くまで子の呼び出し自体が待つ
		m_Fence->SetEventOnCompletion(value, nullptr);
	}

	void* GetNativeFence() const noexcept override { return m_Fence.Get(); }
	ID3D12Fence* GetD3D12Fence() const noexcept { return m_Fence.Get(); }
private:
	ComPtr<ID3D12Fence> m_Fence;
};
