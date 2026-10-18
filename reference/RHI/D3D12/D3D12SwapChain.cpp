/*****************************************************************//**
 * \file   D3D12SwapChain.cpp
 * \brief  IRHISwapChain の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#include "D3D12SwapChain.hpp"
#include "D3D12Device.hpp"
#include "../../Logger.hpp"

#include <cstdio>

D3D12SwapChain::D3D12SwapChain(D3D12Device& device)
	: m_Device(device)
{
}

D3D12SwapChain::~D3D12SwapChain()
{
	// バックバッファの参照を先に外してから IDXGISwapChain を解放する
	m_BackBuffers.clear();
}

bool D3D12SwapChain::Init(const RHISwapChainDesc& desc)
{
	if (desc.windowHandle == nullptr || desc.width == 0 || desc.height == 0 || desc.bufferCount < 2)
	{
		assert(!"CreateSwapChain: 未対応の RHISwapChainDesc");
		return false;
	}

	m_BufferCount	= desc.bufferCount;
	m_Width			= desc.width;
	m_Height		= desc.height;
	m_Format		= desc.format;
	m_VSync			= desc.vSync;

	// VSync を切るときは、ティアリング（垂直同期を待たない表示）が使えるか調べる
	if (!m_VSync)
	{
		ComPtr<IDXGIFactory5> factory5;
		BOOL allow = FALSE;
		if (SUCCEEDED(m_Device.GetFactory()->QueryInterface(IID_PPV_ARGS(factory5.GetAddressOf()))) &&
			SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
		{
			m_AllowTearing = (allow == TRUE);
		}
	}

	DXGI_SWAP_CHAIN_DESC1 sd{};
	sd.Width			= m_Width;
	sd.Height			= m_Height;
	sd.Format			= ToDXGISwapChainFormat(m_Format);	// sRGB は UNORM で作って RTV 側で付ける
	sd.SampleDesc.Count	= 1;
	sd.BufferUsage		= DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount		= m_BufferCount;
	sd.Scaling			= DXGI_SCALING_STRETCH;
	sd.SwapEffect		= DXGI_SWAP_EFFECT_FLIP_DISCARD;	// 毎フレーム描き直すので前の内容は要らない
	sd.AlphaMode		= DXGI_ALPHA_MODE_IGNORE;
	sd.Flags			= m_AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain1;
	if (FAILED(m_Device.GetFactory()->CreateSwapChainForHwnd(
			m_Device.GetQueue(),	// D3D12 ではデバイスではなくキューを渡す
			static_cast<HWND>(desc.windowHandle),
			&sd, nullptr, nullptr, swapChain1.GetAddressOf())) ||
		FAILED(swapChain1.As(&m_SwapChain)))
	{
		LOG->LogError("[RHI] スワップチェインの生成に失敗");
		return false;
	}

	return CreateBackBuffers();
}

bool D3D12SwapChain::CreateBackBuffers()
{
	m_BackBuffers.clear();
	m_BackBuffers.reserve(m_BufferCount);

	for (uint32_t i = 0; i < m_BufferCount; ++i)
	{
		ComPtr<ID3D12Resource> resource;
		if (FAILED(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(resource.GetAddressOf()))))
		{
			return false;
		}

		char name[32];
		std::snprintf(name, sizeof(name), "BackBuffer[%u]", i);
		SetD3D12DebugName(resource.Get(), name);

		RHITextureDesc td;
		td.width	= m_Width;
		td.height	= m_Height;
		td.format	= m_Format;
		td.usage	= ERHITextureUsage::RenderTarget;

		auto texture = MakeUnique<D3D12Texture>(m_Device, td, std::move(resource));
		if (!texture->CreateViews())
		{
			return false;
		}
		m_BackBuffers.push_back(std::move(texture));
	}
	return true;
}

uint32_t D3D12SwapChain::AcquireNextImage()
{
	// D3D12 は Present した順に回るので、今の番号を返すだけでよい
	return m_SwapChain->GetCurrentBackBufferIndex();
}

IRHITexture* D3D12SwapChain::GetBackBuffer(uint32_t index) const
{
	assert(index < m_BackBuffers.size());
	return m_BackBuffers[index].get();
}

ERHIPresentResult D3D12SwapChain::Present()
{
	// ウィンドウモードで本当に同期を外すには ALLOW_TEARING が要る
	const UINT syncInterval	= m_VSync ? 1 : 0;
	const UINT flags		= (!m_VSync && m_AllowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;

	const HRESULT hr = m_SwapChain->Present(syncInterval, flags);

	// DXGI_STATUS_OCCLUDED（最小化などで見えていない）も成功として扱う
	if (SUCCEEDED(hr))
	{
		return ERHIPresentResult::Ok;
	}

	char message[64];
	std::snprintf(message, sizeof(message), "[RHI] Present に失敗 (0x%08X)", static_cast<unsigned>(hr));
	LOG->LogError(message);
	return ERHIPresentResult::Failed;
}

bool D3D12SwapChain::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0) return false;	// 最小化中は作り直さない
	if (width == m_Width && height == m_Height) return true;

	// バックバッファを GPU が使い終わるのを待ってから参照を全部外す
	// （1 つでも残っていると ResizeBuffers が失敗する）
	m_Device.WaitIdle();
	m_BackBuffers.clear();

	if (FAILED(m_SwapChain->ResizeBuffers(
		m_BufferCount, width, height,
		DXGI_FORMAT_UNKNOWN,	// 形式は今のまま
		m_AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0)))	// フラグは作ったときと揃える
	{
		LOG->LogError("[RHI] ResizeBuffers に失敗");
		return false;
	}

	m_Width		= width;
	m_Height	= height;
	return CreateBackBuffers();
}
