/*****************************************************************//**
 * \file   D3D11SwapChain.cpp
 * \brief  IRHISwapChain の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#include "D3D11SwapChain.hpp"

D3D11SwapChain::D3D11SwapChain(
	ComPtr<ID3D11Device> device,
	ComPtr<IDXGISwapChain1> swapChain,
	const RHISwapChainDesc& desc)
	: m_Device(std::move(device))
	, m_SwapChain(std::move(swapChain))
	, m_Desc(desc)
{
}

bool D3D11SwapChain::Init()
{
	return CreateBackBuffers();
}

bool D3D11SwapChain::CreateBackBuffers()
{
	m_BackBuffers.clear();

	// スワップチェーンからバックバッファテクスチャを取得
	ComPtr<ID3D11Texture2D> backBufferTex;
	HRESULT hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(backBufferTex.GetAddressOf()));
	if (FAILED(hr)) return false;

	RHITextureDesc td{};
	td.width = m_Desc.width;
	td.height = m_Desc.height;
	td.format = m_Desc.format;
	td.usage = ERHITextureUsage::RenderTarget;
	td.debugName = "D3D11BackBuffer";

	auto tex = std::make_shared<D3D11Texture>(td, std::move(backBufferTex));
	if (!tex->CreateViews(m_Device.Get())) return false;

	// D3D11 は AcquireNextImage() で常に同じテクスチャ（スロット0）を指す
	for (uint32_t i = 0; i < m_Desc.bufferCount; ++i)
	{
		m_BackBuffers.push_back(tex);
	}

	return true;
}

uint32_t D3D11SwapChain::AcquireNextImage()
{
	return 0;
}

IRHITexture* D3D11SwapChain::GetBackBuffer(uint32_t index) const
{
	if (index >= m_BackBuffers.size()) return nullptr;
	return m_BackBuffers[index].get();
}

ERHIPresentResult D3D11SwapChain::Present()
{
	HRESULT hr = m_SwapChain->Present(m_Desc.vSync ? 1 : 0, 0);
	if (hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_REMOVED)
	{
		return ERHIPresentResult::Failed;
	}
	return ERHIPresentResult::Ok;
}

bool D3D11SwapChain::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0) return false;

	// バックバッファへの参照を全解放してからリサイズ
	m_BackBuffers.clear();

	HRESULT hr = m_SwapChain->ResizeBuffers(
		m_Desc.bufferCount,
		width,
		height,
		ToD3D11Format(m_Desc.format),
		0);
	if (FAILED(hr)) return false;

	m_Desc.width = width;
	m_Desc.height = height;
	return CreateBackBuffers();
}
