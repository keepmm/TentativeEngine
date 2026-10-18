#pragma once

#include "Defines.hpp"


class ConstantBufferAllocator
{
public:
	void Init();

	/// @brief 1 フレームで使った量のピーク(バイト)。リングのサイズを決める目安
	UINT PeakBytes() const noexcept { return m_PeakBytes; }
	static constexpr UINT CapacityBytes() noexcept { return RING_BYTES; }
	void Reset(_In_ UINT frameSlot);

	D3D12_GPU_VIRTUAL_ADDRESS Allocate(
		_In_ UINT frameSlot,
		_In_ const void* data,
		_In_ size_t size
	);

private:
	static constexpr UINT FRAME_COUNT = RTV_NUM;
	// 空シーンの実測ピーク 0.25MB。重いシーンでもメモリパネルの「定数バッファ ピーク」が
	// 容量の半分を超えるようなら上げる(溢れるとその描画が抜ける)
	static constexpr UINT RING_BYTES = 4 * 1024 * 1024; // 4MB
	ComPtr<ID3D12Resource> m_Buffer[FRAME_COUNT];
	uint8_t* m_MappedData[FRAME_COUNT] = {};
	UINT m_Offset[FRAME_COUNT] = {};
	UINT m_PeakBytes = 0;
};

