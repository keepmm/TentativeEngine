/*****************************************************************//**
 * \file   D3D11SwapChain.hpp
 * \brief  IRHISwapChain の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include "D3D11Types.hpp"
#include "D3D11Resource.hpp"
#include "../IRHISwapChain.hpp"

class D3D11SwapChain final : public IRHISwapChain
{
public:
	D3D11SwapChain(
		_In_ ComPtr<ID3D11Device> device,
		_In_ ComPtr<IDXGISwapChain1> swapChain,
		_In_ const RHISwapChainDesc& desc);
	~D3D11SwapChain() override = default;

	bool Init();

	// ---- IRHISwapChain ---- //
	uint32_t AcquireNextImage() override;
	IRHITexture* GetBackBuffer(_In_ uint32_t index) const override;
	ERHIPresentResult Present() override;
	bool Resize(_In_ uint32_t width, _In_ uint32_t height) override;

	uint32_t GetBufferCount() const noexcept override { return m_Desc.bufferCount; }
	uint32_t GetWidth() const noexcept override { return m_Desc.width; }
	uint32_t GetHeight() const noexcept override { return m_Desc.height; }
	ERHIFormat GetFormat() const noexcept override { return m_Desc.format; }
	void* GetNativeSwapChain() const noexcept override { return m_SwapChain.Get(); }

private:
	bool CreateBackBuffers();

	ComPtr<ID3D11Device> m_Device;
	ComPtr<IDXGISwapChain1> m_SwapChain;
	RHISwapChainDesc m_Desc;
	std::vector<std::shared_ptr<D3D11Texture>> m_BackBuffers;
};
