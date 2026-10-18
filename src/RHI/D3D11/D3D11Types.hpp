/*****************************************************************//**
 * \file   D3D11Types.hpp
 * \brief  RHI 型 <-> DirectX 11 型の変換ヘルパー群
 *
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include <d3d11.h>
#include <dxgi1_3.h>
#include <cassert>
#include "../RHITypes.hpp"
#include "RHI/RHIBackendCommon.hpp"

// ---- フォーマット変換 ---- //

inline DXGI_FORMAT ToD3D11Format(ERHIFormat format) noexcept
{
	switch (format)
	{
	case ERHIFormat::R8G8B8A8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
	case ERHIFormat::R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case ERHIFormat::B8G8R8A8_UNORM:      return DXGI_FORMAT_B8G8R8A8_UNORM;
	case ERHIFormat::B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case ERHIFormat::R10G10B10A2_UNORM:   return DXGI_FORMAT_R10G10B10A2_UNORM;
	case ERHIFormat::R11G11B10_FLOAT:     return DXGI_FORMAT_R11G11B10_FLOAT;
	case ERHIFormat::R16_UINT:            return DXGI_FORMAT_R16_UINT;
	case ERHIFormat::R16G16B16A16_FLOAT:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case ERHIFormat::R32_UINT:            return DXGI_FORMAT_R32_UINT;
	case ERHIFormat::R32_FLOAT:           return DXGI_FORMAT_R32_FLOAT;
	case ERHIFormat::R32G32_FLOAT:        return DXGI_FORMAT_R32G32_FLOAT;
	case ERHIFormat::R32G32B32_FLOAT:     return DXGI_FORMAT_R32G32B32_FLOAT;
	case ERHIFormat::R32G32B32A32_FLOAT:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case ERHIFormat::R32G32B32A32_UINT:   return DXGI_FORMAT_R32G32B32A32_UINT;
	case ERHIFormat::D32_FLOAT:           return DXGI_FORMAT_D32_FLOAT;
	case ERHIFormat::BC1_UNORM:           return DXGI_FORMAT_BC1_UNORM;
	case ERHIFormat::BC1_UNORM_SRGB:      return DXGI_FORMAT_BC1_UNORM_SRGB;
	case ERHIFormat::BC3_UNORM:           return DXGI_FORMAT_BC3_UNORM;
	case ERHIFormat::BC3_UNORM_SRGB:      return DXGI_FORMAT_BC3_UNORM_SRGB;
	case ERHIFormat::BC5_UNORM:           return DXGI_FORMAT_BC5_UNORM;
	case ERHIFormat::BC7_UNORM:           return DXGI_FORMAT_BC7_UNORM;
	case ERHIFormat::BC7_UNORM_SRGB:      return DXGI_FORMAT_BC7_UNORM_SRGB;
	default:                              return DXGI_FORMAT_UNKNOWN;
	}
}

// 深度ステンシルとして作りつつシェーダーリソースとしても読む場合は TYPELESS にする
inline DXGI_FORMAT ToD3D11ResourceFormat(ERHIFormat f, ERHITextureUsage usage) noexcept
{
	if (f == ERHIFormat::D32_FLOAT && HasAnyFlag(usage, ERHITextureUsage::ShaderResource))
	{
		return DXGI_FORMAT_R32_TYPELESS;
	}
	return ToD3D11Format(f);
}

// 深度バッファを SRV で読むときは R32_FLOAT として解釈する
inline DXGI_FORMAT ToD3D11SrvFormat(ERHIFormat f) noexcept
{
	if (f == ERHIFormat::D32_FLOAT) return DXGI_FORMAT_R32_FLOAT;
	return ToD3D11Format(f);
}

// ---- バッファとメモリ種別の変換 ---- //

inline D3D11_USAGE ToD3D11Usage(ERHIMemoryType memory) noexcept
{
	switch (memory)
	{
	case ERHIMemoryType::Upload:   return D3D11_USAGE_DYNAMIC;   // CPU から頻繁に書き換える (Map/Unmap)
	case ERHIMemoryType::ReadBack: return D3D11_USAGE_STAGING;   // GPU から CPU へ読み出す
	case ERHIMemoryType::GpuOnly:
	default:                       return D3D11_USAGE_DEFAULT;   // GPU 専用 (UpdateSubresource / Copy)
	}
}

inline UINT ToD3D11CPUAccessFlags(ERHIMemoryType memory) noexcept
{
	switch (memory)
	{
	case ERHIMemoryType::Upload:   return D3D11_CPU_ACCESS_WRITE;
	case ERHIMemoryType::ReadBack: return D3D11_CPU_ACCESS_READ;
	default:                       return 0;
	}
}

inline UINT ToD3D11BufferBindFlags(ERHIBufferUsage usage) noexcept
{
	UINT flags = 0;
	if (HasAnyFlag(usage, ERHIBufferUsage::VertexBuffer))   flags |= D3D11_BIND_VERTEX_BUFFER;
	if (HasAnyFlag(usage, ERHIBufferUsage::IndexBuffer))    flags |= D3D11_BIND_INDEX_BUFFER;
	if (HasAnyFlag(usage, ERHIBufferUsage::ConstantBuffer)) flags |= D3D11_BIND_CONSTANT_BUFFER;
	if (HasAnyFlag(usage, ERHIBufferUsage::ShaderResource)) flags |= D3D11_BIND_SHADER_RESOURCE;
	if (HasAnyFlag(usage, ERHIBufferUsage::UnorderedAccess))flags |= D3D11_BIND_UNORDERED_ACCESS;

	// D3D11 制約: USAGE_DYNAMIC バッファは少なくとも1つの BindFlag が必須（0 は E_INVALIDARG）
	// CopySrc 等のステージング用バッファには VERTEX_BUFFER をデフォルト割り当て
	if (flags == 0)
	{
		flags = D3D11_BIND_VERTEX_BUFFER;
	}
	return flags;
}

inline UINT ToD3D11TextureBindFlags(ERHITextureUsage usage) noexcept
{
	UINT flags = 0;
	if (HasAnyFlag(usage, ERHITextureUsage::ShaderResource))  flags |= D3D11_BIND_SHADER_RESOURCE;
	if (HasAnyFlag(usage, ERHITextureUsage::RenderTarget))    flags |= D3D11_BIND_RENDER_TARGET;
	if (HasAnyFlag(usage, ERHITextureUsage::DepthStencil))    flags |= D3D11_BIND_DEPTH_STENCIL;
	if (HasAnyFlag(usage, ERHITextureUsage::UnorderedAccess)) flags |= D3D11_BIND_UNORDERED_ACCESS;
	return flags;
}

// ---- パイプラインステートの変換 ---- //

inline D3D11_FILL_MODE ToD3D11FillMode(ERHIFillMode mode) noexcept
{
	return (mode == ERHIFillMode::Wireframe) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
}

inline D3D11_CULL_MODE ToD3D11CullMode(ERHICullMode mode) noexcept
{
	switch (mode)
	{
	case ERHICullMode::None:  return D3D11_CULL_NONE;
	case ERHICullMode::Front: return D3D11_CULL_FRONT;
	case ERHICullMode::Back:
	default:                  return D3D11_CULL_BACK;
	}
}

inline D3D11_COMPARISON_FUNC ToD3D11Comparison(ERHICompareFunc func) noexcept
{
	switch (func)
	{
	case ERHICompareFunc::Never:        return D3D11_COMPARISON_NEVER;
	case ERHICompareFunc::Less:         return D3D11_COMPARISON_LESS;
	case ERHICompareFunc::Equal:        return D3D11_COMPARISON_EQUAL;
	case ERHICompareFunc::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
	case ERHICompareFunc::Greater:      return D3D11_COMPARISON_GREATER;
	case ERHICompareFunc::NotEqual:     return D3D11_COMPARISON_NOT_EQUAL;
	case ERHICompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
	case ERHICompareFunc::Always:
	default:                            return D3D11_COMPARISON_ALWAYS;
	}
}

inline D3D11_FILTER ToD3D11Filter(ERHIFilter filter, bool compare) noexcept
{
	if (compare)
	{
		switch (filter)
		{
		case ERHIFilter::Point:       return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		case ERHIFilter::Linear:      return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		case ERHIFilter::Anisotropic: return D3D11_FILTER_COMPARISON_ANISOTROPIC;
		}
	}
	else
	{
		switch (filter)
		{
		case ERHIFilter::Point:       return D3D11_FILTER_MIN_MAG_MIP_POINT;
		case ERHIFilter::Linear:      return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		case ERHIFilter::Anisotropic: return D3D11_FILTER_ANISOTROPIC;
		}
	}
	return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
}

inline D3D11_TEXTURE_ADDRESS_MODE ToD3D11AddressMode(ERHIAddressMode mode) noexcept
{
	switch (mode)
	{
	case ERHIAddressMode::Clamp:  return D3D11_TEXTURE_ADDRESS_CLAMP;
	case ERHIAddressMode::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
	case ERHIAddressMode::Border: return D3D11_TEXTURE_ADDRESS_BORDER;
	case ERHIAddressMode::Wrap:
	default:                      return D3D11_TEXTURE_ADDRESS_WRAP;
	}
}