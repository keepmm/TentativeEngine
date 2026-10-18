/*****************************************************************//**
 * \file   D3D12DescriptorHeap.hpp
 * \brief  デスクリプタヒープ
 *         CPU 専用: テクスチャのビュー置き場（RTV / DSV / コピー元の SRV）
 *         シェーダー可視: デスクリプタテーブルの置き場（描画で使う SRV）
 *
 * 作成者 keepmm
 * 作成日 2026/9/30
 * 更新履歴 9.30 作成
 *			10.15 シェーダー可視ヒープと連続確保（AllocateRange）に対応
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"

class D3D12DescriptorHeap
{
public:
	static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

	/// @param shaderVisible true ならシェーダー可視（GPU ハンドルが使える）
	bool Init(
		_In_ ID3D12Device* device,
		_In_ D3D12_DESCRIPTOR_HEAP_TYPE type,
		_In_ uint32_t capacity,
		_In_ bool shaderVisible = false
	)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = type;
		desc.NumDescriptors = capacity;
		desc.Flags = shaderVisible
			? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
			: D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_Heap.ReleaseAndGetAddressOf()))))
		{
			return false;
		}
		m_Start = m_Heap->GetCPUDescriptorHandleForHeapStart();
		m_GpuStart = shaderVisible ? m_Heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
		m_Stride = device->GetDescriptorHandleIncrementSize(type);
		m_Capacity = capacity;
		m_Next = 0;
		m_FreeRanges.clear();
		return true;
	}

	/// @brief 1 スロット確保する
	/// @return 満杯なら INVALID_INDEX
	uint32_t Allocate() { return AllocateRange(1); }

	/// @brief INVALID_INDEX を渡しても何もしない
	void Free(_In_ uint32_t index) { FreeRange(index, 1); }

	/// @brief 連続した count 個を確保する（デスクリプタテーブル用）
	/// @return 先頭の番号。満杯なら INVALID_INDEX
	uint32_t AllocateRange(_In_ uint32_t count)
	{
		if (count == 0) return INVALID_INDEX;

		std::lock_guard<std::mutex> lock(m_Mutex);

		// 同じ個数で返された範囲があれば使い回す（テーブルの大きさは数種類しかない）
		auto it = m_FreeRanges.find(count);
		if (it != m_FreeRanges.end() && !it->second.empty())
		{
			const uint32_t start = it->second.back();
			it->second.pop_back();
			return start;
		}

		if (m_Next + count > m_Capacity) return INVALID_INDEX;
		const uint32_t start = m_Next;
		m_Next += count;
		return start;
	}

	/// @brief AllocateRange で得た範囲を返す。INVALID_INDEX を渡しても何もしない
	void FreeRange(_In_ uint32_t start, _In_ uint32_t count)
	{
		if (start >= m_Capacity || count == 0) return;

		std::lock_guard<std::mutex> lock(m_Mutex);
		m_FreeRanges[count].push_back(start);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Cpu(_In_ uint32_t index) const noexcept
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_Start;
		h.ptr += static_cast<SIZE_T>(index) * m_Stride;
		return h;
	}

	/// @brief シェーダー可視ヒープのときだけ有効
	D3D12_GPU_DESCRIPTOR_HANDLE Gpu(_In_ uint32_t index) const noexcept
	{
		D3D12_GPU_DESCRIPTOR_HANDLE h = m_GpuStart;
		h.ptr += static_cast<UINT64>(index) * m_Stride;
		return h;
	}

	ID3D12DescriptorHeap* GetHeap() const noexcept { return m_Heap.Get(); }

private:
	ComPtr<ID3D12DescriptorHeap>	m_Heap;
	D3D12_CPU_DESCRIPTOR_HANDLE		m_Start{};
	D3D12_GPU_DESCRIPTOR_HANDLE		m_GpuStart{};
	uint32_t						m_Stride = 0;
	uint32_t						m_Capacity = 0;
	uint32_t						m_Next = 0;
	std::unordered_map<uint32_t, std::vector<uint32_t>> m_FreeRanges;	// 個数ごとの空き範囲
	std::mutex						m_Mutex;
};
