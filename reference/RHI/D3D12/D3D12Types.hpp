/*****************************************************************//**
 * \file   D3D12Types.hpp
 * \brief  RHI型 <-> D3D12 型の変換
 *		　　D3D12バックエンドのなかだけで include する
 * 
 * 作成者 keepmm
 * 作成日 2026/9/30
 * 更新履歴 9.30
 * *********************************************************************/
#pragma once

#include "../../Defines.hpp"
#include "../RHITypes.hpp"

// ---- フォーマット ---- //
inline DXGI_FORMAT ToDXGIFormat(ERHIFormat f) noexcept
{
	switch (f)
	{
	case ERHIFormat::R8G8B8A8_UNORM:		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case ERHIFormat::R8G8B8A8_UNORM_SRGB:	return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case ERHIFormat::B8G8R8A8_UNORM:		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case ERHIFormat::B8G8R8A8_UNORM_SRGB:	return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case ERHIFormat::R10G10B10A2_UNORM:		return DXGI_FORMAT_R10G10B10A2_UNORM;
	case ERHIFormat::R11G11B10_FLOAT:		return DXGI_FORMAT_R11G11B10_FLOAT;
	case ERHIFormat::R16_UINT:				return DXGI_FORMAT_R16_UINT;
	case ERHIFormat::R16G16B16A16_FLOAT:	return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case ERHIFormat::R32_UINT:				return DXGI_FORMAT_R32_UINT;
	case ERHIFormat::R32_FLOAT:				return DXGI_FORMAT_R32_FLOAT;
	case ERHIFormat::R32G32_FLOAT:			return DXGI_FORMAT_R32G32_FLOAT;
	case ERHIFormat::R32G32B32_FLOAT:		return DXGI_FORMAT_R32G32B32_FLOAT;
	case ERHIFormat::R32G32B32A32_FLOAT:	return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case ERHIFormat::R32G32B32A32_UINT:		return DXGI_FORMAT_R32G32B32A32_UINT;
	case ERHIFormat::D32_FLOAT:				return DXGI_FORMAT_D32_FLOAT;
	case ERHIFormat::BC1_UNORM:				return DXGI_FORMAT_BC1_UNORM;
	case ERHIFormat::BC1_UNORM_SRGB:		return DXGI_FORMAT_BC1_UNORM_SRGB;
	case ERHIFormat::BC3_UNORM:				return DXGI_FORMAT_BC3_UNORM;
	case ERHIFormat::BC3_UNORM_SRGB:		return DXGI_FORMAT_BC3_UNORM_SRGB;
	case ERHIFormat::BC5_UNORM:				return DXGI_FORMAT_BC5_UNORM;
	case ERHIFormat::BC7_UNORM:				return DXGI_FORMAT_BC7_UNORM;
	case ERHIFormat::BC7_UNORM_SRGB:		return DXGI_FORMAT_BC7_UNORM_SRGB;
	default:								return DXGI_FORMAT_UNKNOWN;
	}
}

/// @brief DXGI -> RHI
inline ERHIFormat FromDXGIFormat(DXGI_FORMAT f)noexcept
{
	for (uint8_t i = 0; i < static_cast<uint8_t>(ERHIFormat::Count); ++i)
	{
		const ERHIFormat rhi = static_cast<ERHIFormat>(i);
		if (ToDXGIFormat(rhi) == f) return rhi;
	}
	return ERHIFormat::Unknown;
}

/// @brief リソース本体のフォーマット 深度をSRVでも読むときだけTYPELESSにする
inline DXGI_FORMAT ToDXGIResourceFormat(ERHIFormat f, ERHITextureUsage usage)noexcept
{
	if (f == ERHIFormat::D32_FLOAT && HasAnyFlag(usage, ERHITextureUsage::ShaderResource))
	{
		return DXGI_FORMAT_R32_TYPELESS;
	}

	return ToDXGIFormat(f);
}

/// @brief SRVのフォーマット 深度は同じビット列を色として読む
inline DXGI_FORMAT ToDXGISrvFormat(ERHIFormat f)noexcept
{
	if (f == ERHIFormat::D32_FLOAT) return DXGI_FORMAT_R32_FLOAT;
	return ToDXGIFormat(f);
}

/// @brief スワップチェインのバッファ形式
/// @brief flip モデルは sRGB 形式でバッファを作れないので UNORM にする（sRGB は RTV 側で付ける）
inline DXGI_FORMAT ToDXGISwapChainFormat(ERHIFormat f) noexcept
{
	switch (f)
	{
	case ERHIFormat::R8G8B8A8_UNORM_SRGB:	return DXGI_FORMAT_R8G8B8A8_UNORM;
	case ERHIFormat::B8G8R8A8_UNORM_SRGB:	return DXGI_FORMAT_B8G8R8A8_UNORM;
	default:								return ToDXGIFormat(f);
	}
}

// ---- リソース状態 ---- //
inline D3D12_RESOURCE_STATES ToD3D12ResourceStates(ERHIResourceState s) noexcept
{
	struct Pair { ERHIResourceState rhi; D3D12_RESOURCE_STATES d3d; };
	static constexpr Pair TABLE[] =
	{
		{ ERHIResourceState::VertexBuffer,			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER },
		{ ERHIResourceState::ConstantBuffer,		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER },
		{ ERHIResourceState::IndexBuffer,			D3D12_RESOURCE_STATE_INDEX_BUFFER },
		{ ERHIResourceState::ShaderResourcePS,		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
		{ ERHIResourceState::ShaderResourceNonPS,	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
		{ ERHIResourceState::UnorderedAccess,		D3D12_RESOURCE_STATE_UNORDERED_ACCESS },
		{ ERHIResourceState::RenderTarget,			D3D12_RESOURCE_STATE_RENDER_TARGET },
		{ ERHIResourceState::DepthWrite,			D3D12_RESOURCE_STATE_DEPTH_WRITE },
		{ ERHIResourceState::DepthRead,				D3D12_RESOURCE_STATE_DEPTH_READ },
		{ ERHIResourceState::CopySrc,				D3D12_RESOURCE_STATE_COPY_SOURCE },
		{ ERHIResourceState::CopyDst,				D3D12_RESOURCE_STATE_COPY_DEST },
		{ ERHIResourceState::IndirectArgument,		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT },
	};

	// Undefined / Common / Present は D3D12 ではどれも 0(COMMON)
	D3D12_RESOURCE_STATES r = D3D12_RESOURCE_STATE_COMMON;
	for (const Pair& p : TABLE)
	{
		if (HasAnyFlag(s, p.rhi)) r |= p.d3d;
	}
	return r;
}

// ---- メモリ 用途フラグ ---- //

inline D3D12_HEAP_TYPE ToD3D12HeapType(ERHIMemoryType m) noexcept
{
	switch (m)
	{
	case ERHIMemoryType::Upload: return D3D12_HEAP_TYPE_UPLOAD;
	case ERHIMemoryType::ReadBack: return D3D12_HEAP_TYPE_READBACK;
	default: return D3D12_HEAP_TYPE_DEFAULT;
	}
}

inline D3D12_RESOURCE_STATES GetInitialBufferState(ERHIMemoryType m)noexcept
{
	switch (m)
	{
	case ERHIMemoryType::Upload:return D3D12_RESOURCE_STATE_GENERIC_READ;
	case ERHIMemoryType::ReadBack:return D3D12_RESOURCE_STATE_COPY_DEST;
	default: return D3D12_RESOURCE_STATE_COMMON;
	}
}

inline D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(ERHITextureUsage u)noexcept
{
	D3D12_RESOURCE_FLAGS f = D3D12_RESOURCE_FLAG_NONE;
	if (HasAnyFlag(u, ERHITextureUsage::RenderTarget))		f |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (HasAnyFlag(u, ERHITextureUsage::UnorderedAccess))	f |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (HasAnyFlag(u, ERHITextureUsage::DepthStencil))
	{
		f |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		// SRV で読まない深度は明示しておくと、ドライバが圧縮などで最適化できる
		if (!HasAnyFlag(u, ERHITextureUsage::ShaderResource))
		{
			f |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
	}
	return f;
}

inline D3D12_RESOURCE_FLAGS ToD3D12ResourceFlags(ERHIBufferUsage u) noexcept
{
	// D3D12 のバッファは UAV の許可だけ
	return HasAnyFlag(u, ERHIBufferUsage::UnorderedAccess)
		? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		: D3D12_RESOURCE_FLAG_NONE;
}

// ---- プリミティブ ---- //
inline D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12TopologyType(ERHIPrimitiveTopology t) noexcept
{
	switch(t)
	{
	case ERHIPrimitiveTopology::LineList:
	case ERHIPrimitiveTopology::LineStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	case ERHIPrimitiveTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}
}

inline D3D_PRIMITIVE_TOPOLOGY ToD3D12Topology(ERHIPrimitiveTopology t)noexcept
{
	switch (t)
	{
	case ERHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	case ERHIPrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
	case ERHIPrimitiveTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
	case ERHIPrimitiveTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}
}

// ---- ラスタライザ / 深度 ---- //
inline D3D12_CULL_MODE ToD3D12CullMode(ERHICullMode c)noexcept
{
	switch (c)
	{
	case ERHICullMode::None: return D3D12_CULL_MODE_NONE;
	case ERHICullMode::Front: return D3D12_CULL_MODE_FRONT;
	default: return D3D12_CULL_MODE_BACK;
	}
}

inline D3D12_FILL_MODE ToD3D12FillMode(ERHIFillMode f)noexcept
{
	return f == ERHIFillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
}

inline D3D12_COMPARISON_FUNC ToD3D12Comparison(ERHICompareFunc c)noexcept
{
	switch (c)
	{
	case ERHICompareFunc::Never:		return D3D12_COMPARISON_FUNC_NEVER;
	case ERHICompareFunc::Less:			return D3D12_COMPARISON_FUNC_LESS;
	case ERHICompareFunc::Equal:		return D3D12_COMPARISON_FUNC_EQUAL;
	case ERHICompareFunc::LessEqual:	return D3D12_COMPARISON_FUNC_LESS_EQUAL;
	case ERHICompareFunc::Greater:		return D3D12_COMPARISON_FUNC_GREATER;
	case ERHICompareFunc::NotEqual:		return D3D12_COMPARISON_FUNC_NOT_EQUAL;
	case ERHICompareFunc::GreaterEqual:	return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	default:							return D3D12_COMPARISON_FUNC_ALWAYS;
	}
}

// ---- ブレンド ---- //

inline D3D12_BLEND ToD3D12Blend(ERHIBlendFactor b)noexcept
{
	switch (b)
	{
	case ERHIBlendFactor::Zero:			return D3D12_BLEND_ZERO;
	case ERHIBlendFactor::One:			return D3D12_BLEND_ONE;
	case ERHIBlendFactor::SrcColor:		return D3D12_BLEND_SRC_COLOR;
	case ERHIBlendFactor::InvSrcColor:	return D3D12_BLEND_INV_SRC_COLOR;
	case ERHIBlendFactor::SrcAlpha:		return D3D12_BLEND_SRC_ALPHA;
	case ERHIBlendFactor::InvSrcAlpha:	return D3D12_BLEND_INV_SRC_ALPHA;
	case ERHIBlendFactor::DstColor:		return D3D12_BLEND_DEST_COLOR;
	case ERHIBlendFactor::InvDstColor:	return D3D12_BLEND_INV_DEST_COLOR;
	case ERHIBlendFactor::DstAlpha:		return D3D12_BLEND_DEST_ALPHA;
	case ERHIBlendFactor::InvDstAlpha:	return D3D12_BLEND_INV_DEST_ALPHA;
	default:							return D3D12_BLEND_ONE;
	}
}

inline D3D12_BLEND_OP ToD3D12BlendOp(ERHIBlendOp op) noexcept
{
	switch (op)
	{
	case ERHIBlendOp::Subtract:		return D3D12_BLEND_OP_SUBTRACT;
	case ERHIBlendOp::RevSubtract:	return D3D12_BLEND_OP_REV_SUBTRACT;
	case ERHIBlendOp::Min:			return D3D12_BLEND_OP_MIN;
	case ERHIBlendOp::Max:			return D3D12_BLEND_OP_MAX;
	default:						return D3D12_BLEND_OP_ADD;
	}
}

/// @brief R=1 G=2 B=4 A=8 は D3D12_COLOR_WRITE_ENABLE と同じ並びなのでそのまま渡せる
inline UINT8 ToD3D12WriteMask(ERHIColorWriteMask m) noexcept
{
	return static_cast<UINT8>(m);
}

// ---- サンプラー ---- //
inline D3D12_FILTER ToD3D12Filter(ERHIFilter f, bool compare)noexcept
{
	switch (f)
	{
	case ERHIFilter::Point:
		return compare ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;
	case ERHIFilter::Anisotropic:
		return compare ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
	default:
		return compare ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	}
}

inline D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(ERHIAddressMode a)noexcept
{
	switch(a)
	{
	case ERHIAddressMode::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	case ERHIAddressMode::Wrap: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	case ERHIAddressMode::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}
}
inline D3D12_STATIC_BORDER_COLOR ToD3D12BorderColor(ERHIBorderColor c) noexcept
{
	switch (c)
	{
	case ERHIBorderColor::TransparentBlack:	return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	case ERHIBorderColor::OpaqueBlack:		return D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	default:								return D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	}
}

/// @brief D3D12 は「1 ステージだけ」か「全部」しか選べない
inline D3D12_SHADER_VISIBILITY ToD3D12Visibility(ERHIShaderStage s) noexcept
{
	if (s == ERHIShaderStage::Vertex)	return D3D12_SHADER_VISIBILITY_VERTEX;
	if (s == ERHIShaderStage::Pixel)	return D3D12_SHADER_VISIBILITY_PIXEL;
	return D3D12_SHADER_VISIBILITY_ALL;
}

// ---------------------------------------------------//
//					ビューポート / 矩形				  //
// ---------------------------------------------------//
inline D3D12_VIEWPORT ToD3D12Viewport(const RHIViewport& v) noexcept
{
	return { v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth };
}

inline D3D12_RECT ToD3D12Rect(const RHIRect& r) noexcept
{
	return { r.left, r.top, r.right, r.bottom };
}

// ---------------------------------------------------//
//						デバッグ名					  //
// ---------------------------------------------------//
/// @brief PIX / デバッグレイヤーに出る名前を付ける（UTF-8 → UTF-16。127 文字まで）
inline void SetD3D12DebugName(ID3D12Object* obj, const char* name)
{
	if (obj == nullptr || name == nullptr || name[0] == '\0') return;

	wchar_t buf[128];
	if (MultiByteToWideChar(CP_UTF8, 0, name, -1, buf, _countof(buf)) > 0)
	{
		obj->SetName(buf);
	}
}