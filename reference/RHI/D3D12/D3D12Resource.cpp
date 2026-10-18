/*****************************************************************//**
 * \file   D3D12Resource.cpp
 * \brief  IRHITexture / IRHIBuffer の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/9/30
 * 更新履歴 9.30 作成
 * *********************************************************************/
#include "D3D12Resource.hpp"
#include "D3D12Device.hpp"

 // ---------------------------------------------------//
 //						D3D12Texture					  //
 // ---------------------------------------------------//
D3D12Texture::D3D12Texture(
	D3D12Device& device,
	const RHITextureDesc& desc,
	ComPtr<ID3D12Resource> resource)
	: m_Device(device)
	, m_Resource(std::move(resource))
	, m_Desc(desc)
{
	// 呼び出し側の文字列を指したまま持たない
	m_Desc.debugName = nullptr;
}

D3D12Texture::~D3D12Texture()
{
	// 遅延解放を経てここに来る（= GPU は使い終わっている）のでスロットを返してよい
	m_Device.GetSrvHeap().Free(m_SrvIndex);
	m_Device.GetRtvHeap().Free(m_RtvIndex);
	m_Device.GetDsvHeap().Free(m_DsvIndex);
}

bool D3D12Texture::CreateViews()
{
	ID3D12Device* dev = m_Device.GetD3D12Device();

	// Cube も RTV/DSV から見れば 6 枚の配列
	const bool isArray =
		m_Desc.dimension == ERHITextureDimension::Texture2DArray ||
		m_Desc.dimension == ERHITextureDimension::TextureCube;

	// ---- SRV ---- //
	if (HasAnyFlag(m_Desc.usage, ERHITextureUsage::ShaderResource))
	{
		m_SrvIndex = m_Device.GetSrvHeap().Allocate();
		if (m_SrvIndex == D3D12DescriptorHeap::INVALID_INDEX) return false;

		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = ToDXGISrvFormat(m_Desc.format);	// 深度は R32_FLOAT で読む
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		switch (m_Desc.dimension)
		{
		case ERHITextureDimension::Texture2D:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = m_Desc.mipLevels;
			break;
		case ERHITextureDimension::Texture2DArray:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			srv.Texture2DArray.MipLevels = m_Desc.mipLevels;
			srv.Texture2DArray.ArraySize = m_Desc.depthOrArraySize;
			break;
		case ERHITextureDimension::TextureCube:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srv.TextureCube.MipLevels = m_Desc.mipLevels;
			break;
		case ERHITextureDimension::Texture3D:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			srv.Texture3D.MipLevels = m_Desc.mipLevels;
			break;
		}
		m_Srv = m_Device.GetSrvHeap().Cpu(m_SrvIndex);
		dev->CreateShaderResourceView(m_Resource.Get(), &srv, m_Srv);
	}

	// ---- RTV（mip 0・全スライス）---- //
	if (HasAnyFlag(m_Desc.usage, ERHITextureUsage::RenderTarget))
	{
		m_RtvIndex = m_Device.GetRtvHeap().Allocate();
		if (m_RtvIndex == D3D12DescriptorHeap::INVALID_INDEX) return false;

		D3D12_RENDER_TARGET_VIEW_DESC rtv{};
		rtv.Format = ToDXGIFormat(m_Desc.format);
		if (isArray)
		{
			rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			rtv.Texture2DArray.ArraySize = m_Desc.depthOrArraySize;
		}
		else
		{
			rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		}
		m_Rtv = m_Device.GetRtvHeap().Cpu(m_RtvIndex);
		dev->CreateRenderTargetView(m_Resource.Get(), &rtv, m_Rtv);
	}

	// ---- DSV ---- //
	if (HasAnyFlag(m_Desc.usage, ERHITextureUsage::DepthStencil))
	{
		m_DsvIndex = m_Device.GetDsvHeap().Allocate();
		if (m_DsvIndex == D3D12DescriptorHeap::INVALID_INDEX) return false;

		D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
		dsv.Format = ToDXGIFormat(m_Desc.format);	// D32_FLOAT（TYPELESS ではない）
		if (isArray)
		{
			dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			dsv.Texture2DArray.ArraySize = m_Desc.depthOrArraySize;
		}
		else
		{
			dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		}
		m_Dsv = m_Device.GetDsvHeap().Cpu(m_DsvIndex);
		dev->CreateDepthStencilView(m_Resource.Get(), &dsv, m_Dsv);
	}
	return true;
}

// ---------------------------------------------------//
//						D3D12Buffer					  //
// ---------------------------------------------------//
D3D12Buffer::D3D12Buffer(
	const RHIBufferDesc& desc,
	ComPtr<ID3D12Resource> resource)
	: m_Resource(std::move(resource))
	, m_Desc(desc)
{
	m_Desc.debugName = nullptr;

	if (m_Desc.memory == ERHIMemoryType::Upload)
	{
		// Upload ヒープはマップしっぱなしでよい（D3D12 で正式に許されている）
		const D3D12_RANGE noRead{ 0, 0 };	// CPU からは読まない
		if (FAILED(m_Resource->Map(0, &noRead, &m_Persistent)))
		{
			m_Persistent = nullptr;
		}
	}
}

D3D12Buffer::~D3D12Buffer()
{
	if (m_Persistent)
	{
		m_Resource->Unmap(0, nullptr);
	}
}

void* D3D12Buffer::Map()
{
	switch (m_Desc.memory)
	{
	case ERHIMemoryType::Upload:
		return m_Persistent;

	case ERHIMemoryType::ReadBack:
	{
		void* p = nullptr;
		// 読み取り範囲 nullptr = 全域を読む
		if (FAILED(m_Resource->Map(0, nullptr, &p))) return nullptr;
		return p;
	}

	default:
		return nullptr;	// GpuOnly は CPU から触れない
	}
}

void D3D12Buffer::Unmap()
{
	if (m_Desc.memory == ERHIMemoryType::ReadBack)
	{
		const D3D12_RANGE noWrite{ 0, 0 };	// CPU からは書いていない
		m_Resource->Unmap(0, &noWrite);
	}
}