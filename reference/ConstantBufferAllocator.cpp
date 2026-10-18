#include "ConstantBufferAllocator.hpp"
#include "Logger.hpp"
#include "d3dx12.h"
#include <cstring>
#include "DirectX.hpp"

constexpr UINT CBV_ALIGNMENT = 256;

inline UINT AlignUp(UINT value, UINT alignment)
{
	return (value + (alignment - 1)) & ~(alignment - 1);
}

void ConstantBufferAllocator::Reset(UINT frameSlot)
{
	if (frameSlot >= FRAME_COUNT)
	{
		return;
	}

	m_Offset[frameSlot] = 0;
}

D3D12_GPU_VIRTUAL_ADDRESS ConstantBufferAllocator::Allocate(UINT frameSlot, const void* data, size_t size)
{
	// ------------------------ //
	//			引数の検査	   	//
	// ------------------------ //
	if (frameSlot >= FRAME_COUNT)
	{
		return 0;
	}

	if(m_Buffer[frameSlot] == nullptr || m_MappedData[frameSlot] == nullptr)
	{
		LOG->LogError("ConstantBufferAllocator::Allocate: バッファが初期化されていません");
		return 0;
	}

	const UINT alignedSize = AlignUp(static_cast<UINT>(size), CBV_ALIGNMENT);

	// リングが溢れたら確保失敗
	if(m_Offset[frameSlot] + alignedSize > RING_BYTES)
	{
		LOG->LogError("ConstantBufferAllocator::Allocate: リングバッファが溢れました");
		return 0;
	}

	const UINT offset = m_Offset[frameSlot];

	if (data != nullptr)
	{
		std::memcpy(m_MappedData[frameSlot] + offset, data, size);
	}

	m_Offset[frameSlot] += alignedSize;
	if (m_Offset[frameSlot] > m_PeakBytes) m_PeakBytes = m_Offset[frameSlot];

	return m_Buffer[frameSlot]->GetGPUVirtualAddress() + offset;
}

void ConstantBufferAllocator::Init()
{
	auto device = DirectXApp::GetCurrent()->GetDevice();
	if (device == nullptr)
	{
		LOG->LogError("ConstantBufferAllocator::Init: デバイスが取得できませんでした");
	}

	// フレーム数分のuploadバッファを確保し、永続マップしておく
	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(RING_BYTES);

	for (UINT i = 0; i < FRAME_COUNT; ++i)
	{
		const HRESULT hr = device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_Buffer[i])
		);
		if (FAILED(hr))
		{
			m_Buffer[i] = nullptr;
			m_MappedData[i] = nullptr;
			LOG->LogError("ConstantBufferAllocator::Init: バッファの作成に失敗しました");
			continue;
		}

		// 読み取りはしないので範囲は0
		void* mapped = nullptr;
		CD3DX12_RANGE readRange(0, 0);
		if(FAILED(m_Buffer[i]->Map(0,&readRange, &mapped)))
		{
			m_Buffer[i] = nullptr;
			m_MappedData[i] = nullptr;
			LOG->LogError("ConstantBufferAllocator::Init: バッファのマッピングに失敗しました");
			continue;
		}

		m_MappedData[i] = static_cast<uint8_t*>(mapped);
		m_Offset[i] = 0;
	}
}