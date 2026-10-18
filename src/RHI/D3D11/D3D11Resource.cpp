/*****************************************************************//**
 * \file   D3D11Resource.cpp
 * \brief  IRHITexture / IRHIBuffer の DirectX 11 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#include "D3D11Resource.hpp"
#include <cstring>

 // ------------------------------------------------------------------ //
 //                           D3D11Texture                             //
 // ------------------------------------------------------------------ //

D3D11Texture::D3D11Texture(
	const RHITextureDesc& desc,
	ComPtr<ID3D11Texture2D> texture)
	: m_Desc(desc)
	, m_Texture(std::move(texture))
{
	m_Desc.debugName = nullptr;
}

bool D3D11Texture::CreateViews(ID3D11Device* device)
{
	if (!device || !m_Texture) return false;

	// ---- SRV (シェーダー読み取り用) ---- //
	if (HasAnyFlag(m_Desc.usage, ERHITextureUsage::ShaderResource))
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = ToD3D11SrvFormat(m_Desc.format);
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MostDetailedMip = 0;
		srv.Texture2D.MipLevels = (m_Desc.mipLevels == 0) ? 1 : m_Desc.mipLevels;

		if (FAILED(device->CreateShaderResourceView(m_Texture.Get(), &srv, m_Srv.ReleaseAndGetAddressOf())))
		{
			return false;
		}
	}

	// ---- RTV (レンダーターゲット用) ---- //
	if (HasAnyFlag(m_Desc.usage, ERHITextureUsage::RenderTarget))
	{
		D3D11_RENDER_TARGET_VIEW_DESC rtv{};
		rtv.Format = ToD3D11Format(m_Desc.format);
		rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtv.Texture2D.MipSlice = 0;

		if (FAILED(device->CreateRenderTargetView(m_Texture.Get(), &rtv, m_Rtv.ReleaseAndGetAddressOf())))
		{
			return false;
		}
	}

	// ---- DSV (深度ステンシル用) ---- //
	if (HasAnyFlag(m_Desc.usage, ERHITextureUsage::DepthStencil))
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
		dsv.Format = ToD3D11Format(m_Desc.format);
		dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsv.Texture2D.MipSlice = 0;

		if (FAILED(device->CreateDepthStencilView(m_Texture.Get(), &dsv, m_Dsv.ReleaseAndGetAddressOf())))
		{
			return false;
		}
	}

	return true;
}

// ------------------------------------------------------------------ //
//                           D3D11Buffer                              //
// ------------------------------------------------------------------ //

D3D11Buffer::D3D11Buffer(
	const RHIBufferDesc& desc,
	ComPtr<ID3D11Buffer> buffer,
	ComPtr<ID3D11ShaderResourceView> srv)
	: m_Desc(desc)
	, m_Buffer(std::move(buffer))
	, m_Srv(std::move(srv))
{
	m_Desc.debugName = nullptr;

	// Upload（CPU 書き込み）の場合は永続マップ互換のためのシャドウメモリを確保
	if (m_Desc.memory == ERHIMemoryType::Upload)
	{
		m_CpuData.resize(static_cast<size_t>(m_Desc.size), 0);
	}
}

void* D3D11Buffer::Map()
{
	if (m_Desc.memory == ERHIMemoryType::Upload)
	{
		m_IsMapped = true;
		m_IsDirty = true;
		return m_CpuData.data();
	}
	return nullptr;
}

void D3D11Buffer::Unmap()
{
	m_IsMapped = false;
}

void D3D11Buffer::SetInitialData(const void* data, size_t size)
{
	if (!m_CpuData.empty() && data && size > 0)
	{
		std::memcpy(m_CpuData.data(), data, (std::min)(m_CpuData.size(), size));
	}
}

void D3D11Buffer::Flush(ID3D11DeviceContext* context)
{
	// CPU メモリまたはコンテキストがない場合はスキップ
	if (m_CpuData.empty() || !context || !m_Buffer) return;

	// マップ中（永続マップ含む）または Dirty フラグが立っている場合に GPU へ転送
	if (!m_IsMapped && !m_IsDirty) return;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(context->Map(m_Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		std::memcpy(mapped.pData, m_CpuData.data(), m_CpuData.size());
		context->Unmap(m_Buffer.Get(), 0);
		m_IsDirty = false;
	}
}