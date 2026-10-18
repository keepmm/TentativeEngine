/*****************************************************************//**
 * \file   D3D12Resource.hpp
 * \brief  IRHITexture / IRHIBuffer のD3D12 実装
 * 
 * 作成者 keepmmm
 * 作成日 2026/9/30
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "D3D12DescriptorHeap.hpp"
#include "../IRHITexture.hpp"
#include "../IRHIBuffer.hpp"

class D3D12Device;

class D3D12Texture final : public IRHITexture
{
public:
	D3D12Texture(
		_In_ D3D12Device& device,
		_In_ const RHITextureDesc& desc,
		_In_ ComPtr<ID3D12Resource> resource
	);
	~D3D12Texture() override;

	bool CreateViews();

	const RHITextureDesc& GetDesc()const noexcept override { return m_Desc; }
	void* GetNativeResource() const noexcept override { return m_Resource.Get(); }

	ID3D12Resource* GetResource() const noexcept { return m_Resource.Get(); }

	/// @brief CPU 専用ヒープ上のビュー。usage に無いものは ptr == 0
	/// @brief SRV はシェーダー可視ヒープへ CopyDescriptorsSimple してから使う
	D3D12_CPU_DESCRIPTOR_HANDLE GetSrv() const noexcept { return m_Srv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtv() const noexcept { return m_Rtv; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDsv() const noexcept { return m_Dsv; }
private:
	D3D12Device& m_Device;
	ComPtr<ID3D12Resource> m_Resource;
	RHITextureDesc m_Desc;

	uint32_t m_SrvIndex = D3D12DescriptorHeap::INVALID_INDEX;
	uint32_t m_RtvIndex = D3D12DescriptorHeap::INVALID_INDEX;
	uint32_t m_DsvIndex = D3D12DescriptorHeap::INVALID_INDEX;
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Rtv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Dsv{};
};

class D3D12Buffer final : public IRHIBuffer
{
public:
	D3D12Buffer(
		_In_ const RHIBufferDesc& desc,
		_In_ ComPtr<ID3D12Resource> resource
	);

	~D3D12Buffer() override;

	const RHIBufferDesc& GetDesc() const noexcept override { return m_Desc; }

	void* Map() override;
	void Unmap() override;
	void* GetNativeResource() const noexcept override { return m_Resource.Get(); }

	ID3D12Resource* GetResource() const noexcept { return m_Resource.Get(); }

	D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const noexcept { return m_Resource->GetGPUVirtualAddress(); }
private:
	ComPtr<ID3D12Resource> m_Resource;
	RHIBufferDesc m_Desc;
	void* m_Persistent = nullptr;
};
