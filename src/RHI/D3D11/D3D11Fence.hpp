/*****************************************************************//**
 * \file   D3D11Fence.hpp
 * \brief  IRHIFence の DirectX 11 実装（D3D11_QUERY_EVENT による同期）
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include "D3D11Types.hpp"
#include "../IRHIFence.hpp"
#include <unordered_map>

class D3D11Fence final : public IRHIFence
{
public:
	D3D11Fence(_In_ ID3D11Device* device, _In_ uint64_t initialValue)
		: m_Device(device)
		, m_CurrentValue(initialValue)
		, m_CompletedValue(initialValue)
	{
	}

	uint64_t GetCompletedValue() const override
	{
		return m_CompletedValue;
	}

	void Signal(uint64_t value, ID3D11DeviceContext* context)
	{
		m_CurrentValue = value;
		m_Context = context;

		if (!m_Device || !context)
		{
			m_CompletedValue = value;
			return;
		}

		D3D11_QUERY_DESC qd{};
		qd.Query = D3D11_QUERY_EVENT;
		ComPtr<ID3D11Query> query;
		if (SUCCEEDED(m_Device->CreateQuery(&qd, query.GetAddressOf())))
		{
			context->End(query.Get());
			m_PendingQueries[value] = std::move(query);
		}
		else
		{
			m_CompletedValue = value;
		}
	}

	void Wait(uint64_t value) override
	{
		if (value <= m_CompletedValue) return;

		auto it = m_PendingQueries.find(value);
		if (it != m_PendingQueries.end())
		{
			ID3D11Query* q = it->second.Get();
			if (q && m_Context)
			{
				BOOL done = FALSE;
				while (m_Context->GetData(q, &done, sizeof(BOOL), 0) == S_FALSE)
				{
					Sleep(0);
				}
			}
			m_CompletedValue = value;
			m_PendingQueries.erase(it);
		}
		else
		{
			m_CompletedValue = value;
		}
	}

	void* GetNativeFence() const noexcept override { return nullptr; }

private:
	ID3D11Device* m_Device = nullptr;
	ID3D11DeviceContext* m_Context = nullptr;
	uint64_t m_CurrentValue = 0;
	uint64_t m_CompletedValue = 0;
	std::unordered_map<uint64_t, ComPtr<ID3D11Query>> m_PendingQueries;
};
