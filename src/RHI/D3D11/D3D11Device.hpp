/*****************************************************************//**
 * \file   D3D11Device.hpp
 * \brief  IRHIDevice の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include "D3D11Types.hpp"
#include "D3D11Resource.hpp"
#include "D3D11Pipeline.hpp"
#include "D3D11DescriptorTable.hpp"
#include "D3D11CommandList.hpp"
#include "D3D11SwapChain.hpp"
#include "D3D11Fence.hpp"
#include "../IRHIDevice.hpp"

class D3D11Device final : public IRHIDevice
{
public:
	D3D11Device() = default;
	~D3D11Device() override = default;

	bool Init(_In_ const RHIDeviceDesc& desc);

	// ---- IRHIDevice : 情報 ---- //
	ERHIBackend GetBackend() const noexcept override { return ERHIBackend::D3D11; }
	const RHIDeviceCaps& GetCaps() const noexcept override { return m_Caps; }
	uint32_t GetFramesInFlight() const noexcept override { return m_FramesInFlight; }

	// ---- IRHIDevice : リソース作成 ---- //
	RHITextureRef CreateTexture(_In_ const RHITextureDesc& desc) override;
	RHIBufferRef CreateBuffer(_In_ const RHIBufferDesc& desc, _In_opt_ const void* initialData = nullptr) override;
	uint64_t GetCopyableFootprints(
		_In_ IRHITexture* texture,
		_In_ uint32_t firstSubresource,
		_In_ uint32_t numSubresources,
		_Out_writes_(numSubresources) RHISubresourceFootprint* out) override;

	// ---- IRHIDevice : パイプライン作成 ---- //
	RHIPipelineLayoutRef CreatePipelineLayout(_In_ const RHIPipelineLayoutDesc& desc) override;
	RHIPipelineStateRef CreatePipelineState(_In_ const RHIGraphicsPipelineDesc& desc) override;
	RHIDescriptorTableRef CreateDescriptorTable(_In_ IRHIPipelineLayout* layout, _In_ uint32_t slot) override;
	RHICommandListRef CreateCommandList(_In_opt_ const char* debugName = nullptr) override;
	RHIFenceRef CreateFence(_In_ uint64_t initialValue) override;
	RHISwapChainRef CreateSwapChain(_In_ const RHISwapChainDesc& desc) override;

	void Submit(
		_In_reads_(count) IRHICommandList* const* lists,
		_In_ uint32_t count,
		_In_opt_ IRHIFence* signalFence = nullptr,
		_In_ uint64_t signalValue = 0) override;

	void WaitIdle() override;
	void BeginFrame(_In_ uint32_t slot) override;

	void* GetNativeDevice() const noexcept override { return m_Device.Get(); }
	void* GetNativeQueue() const noexcept override { return m_Context.Get(); }

	ID3D11Device* GetD3D11Device() const noexcept { return m_Device.Get(); }
	ID3D11DeviceContext* GetD3D11Context() const noexcept { return m_Context.Get(); }

private:
	ComPtr<ID3D11Device> m_Device;
	ComPtr<ID3D11DeviceContext> m_Context;
	ComPtr<IDXGIFactory2> m_DxgiFactory;
	RHIDeviceCaps m_Caps{};
	uint32_t m_FramesInFlight = 1;
};
