/*****************************************************************//**
 * \file   VulkanTypes.hpp
 * \brief  RHI型 <-> Vulkan型の変換
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

// NOMINMAX は CMake で全体に定義している
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define VNULL VK_NULL_HANDLE

#include "RHI/RHIBackendCommon.hpp"
#include "../RHITypes.hpp"
#include "../IRHICommandList.hpp"

 // ---- フォーマット ---- //
inline VkFormat ToVkFormat(ERHIFormat f) noexcept
{
	switch (f)
	{
	case ERHIFormat::R8G8B8A8_UNORM:		return VK_FORMAT_R8G8B8A8_UNORM;
	case ERHIFormat::R8G8B8A8_UNORM_SRGB:	return VK_FORMAT_R8G8B8A8_SRGB;
	case ERHIFormat::B8G8R8A8_UNORM:		return VK_FORMAT_B8G8R8A8_UNORM;
	case ERHIFormat::B8G8R8A8_UNORM_SRGB:	return VK_FORMAT_B8G8R8A8_SRGB;
	case ERHIFormat::R10G10B10A2_UNORM:		return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	case ERHIFormat::R11G11B10_FLOAT:		return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	case ERHIFormat::R16_UINT:				return VK_FORMAT_R16_UINT;
	case ERHIFormat::R16G16B16A16_FLOAT:	return VK_FORMAT_R16G16B16A16_SFLOAT;
	case ERHIFormat::R32_UINT:				return VK_FORMAT_R32_UINT;
	case ERHIFormat::R32_FLOAT:				return VK_FORMAT_R32_SFLOAT;
	case ERHIFormat::R32G32_FLOAT:			return VK_FORMAT_R32G32_SFLOAT;
	case ERHIFormat::R32G32B32_FLOAT:		return VK_FORMAT_R32G32B32_SFLOAT;
	case ERHIFormat::R32G32B32A32_FLOAT:	return VK_FORMAT_R32G32B32A32_SFLOAT;
	case ERHIFormat::R32G32B32A32_UINT:		return VK_FORMAT_R32G32B32A32_UINT;
	case ERHIFormat::D32_FLOAT:				return VK_FORMAT_D32_SFLOAT;
	//case ERHIFormat::BC1_UNORM:				return VK_FORMAT_BC1_RGBA_UNORM_block;
	//case ERHIFormat::BC1_UNORM_SRGB:		return VK_FORMAT_BC1_RGBA_SRGB_block;
	//case ERHIFormat::BC3_UNORM:				return VK_FORMAT_BC3_UNORM_block;
	//case ERHIFormat::BC3_UNORM_SRGB:		return VK_FORMAT_BC3_SRGB_block;
	//case ERHIFormat::BC5_UNORM:				return VK_FORMAT_BC5_UNORM_block;
	//case ERHIFormat::BC7_UNORM:				return VK_FORMAT_BC7_UNORM_block;
	//case ERHIFormat::BC7_UNORM_SRGB:		return VK_FORMAT_BC7_SRGB_block;
	default:								return VK_FORMAT_UNDEFINED;
	}
}

/// @brief Vulkan -> RHI
inline ERHIFormat FromVkFormat(VkFormat f) noexcept
{
	for (uint8_t i = 0; i < static_cast<uint8_t>(ERHIFormat::Count); ++i)
	{
		const ERHIFormat rhi = static_cast<ERHIFormat>(i);
		if (ToVkFormat(rhi) == f) return rhi;
	}
	return ERHIFormat::Unknown;
}

// ---- テクスチャ用途 ---- //
inline VkImageUsageFlags ToVkImageUsage(ERHITextureUsage u) noexcept
{
	// コピー元・コピー先は常に付けておく（アップロードや読み戻し用）
	VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if (HasAnyFlag(u, ERHITextureUsage::ShaderResource))	flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (HasAnyFlag(u, ERHITextureUsage::RenderTarget))		flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (HasAnyFlag(u, ERHITextureUsage::DepthStencil))		flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (HasAnyFlag(u, ERHITextureUsage::UnorderedAccess))	flags |= VK_IMAGE_USAGE_STORAGE_BIT;
	return flags;
}

// ---- バッファ用途 ---- //
inline VkBufferUsageFlags ToVkBufferUsage(ERHIBufferUsage u) noexcept
{
	VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (HasAnyFlag(u, ERHIBufferUsage::VertexBuffer))		flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (HasAnyFlag(u, ERHIBufferUsage::IndexBuffer))		flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (HasAnyFlag(u, ERHIBufferUsage::ConstantBuffer))		flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if (HasAnyFlag(u, ERHIBufferUsage::ShaderResource))		flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if (HasAnyFlag(u, ERHIBufferUsage::UnorderedAccess))	flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if (HasAnyFlag(u, ERHIBufferUsage::IndirectArgs))		flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	return flags;
}

// ---- メモリ種別 (D3D12_HEAP_TYPE 相当) ---- //
inline VkMemoryPropertyFlags ToVkMemoryFlags(ERHIMemoryType m) noexcept
{
	switch (m)
	{
	case ERHIMemoryType::GpuOnly:
		// GPU 専用の高速 VRAM
		return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	case ERHIMemoryType::Upload:
		// CPU から書き込めて、Flush なしで即 GPU に見えるメモリ
		return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	case ERHIMemoryType::ReadBack:
		// GPU から書き戻して CPU でキャッシュ付きで読むメモリ
		return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	default:
		return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}
}
// ---- リソース状態 ---- //
struct VkStateInfo
{
	VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
	VkAccessFlags2 access = VK_ACCESS_2_NONE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

inline VkStateInfo ToVkStateInfo(ERHIResourceState s, bool isDepth = false) noexcept
{
	switch (s)
	{
	case ERHIResourceState::Undefined:
		return { VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED };
	case ERHIResourceState::Common:
		return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
	case ERHIResourceState::VertexBuffer:
		return { VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
	case ERHIResourceState::IndexBuffer:
		return { VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
	case ERHIResourceState::ConstantBuffer:
		return { VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
	case ERHIResourceState::ShaderResourcePS:
		return { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	case ERHIResourceState::ShaderResourceNonPS:
		return { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	case ERHIResourceState::ShaderResource:
		return { VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	case ERHIResourceState::UnorderedAccess:
		return { VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
	case ERHIResourceState::RenderTarget:
		return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	case ERHIResourceState::DepthWrite:
		return { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	case ERHIResourceState::DepthRead:
		return { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	case ERHIResourceState::CopySrc:
		return { VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
	case ERHIResourceState::CopyDst:
		return { VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
	case ERHIResourceState::IndirectArgument:
		return { VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED };
	case ERHIResourceState::Present:
		return { VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
	default:
		(void)isDepth;
		return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
	}
}
// ---- LoadOp / StoreOp ---- //
inline VkAttachmentLoadOp ToVkLoadOp(ERHILoadOp op) noexcept
{
	switch (op)
	{
	case ERHILoadOp::Load:		return VK_ATTACHMENT_LOAD_OP_LOAD;
	case ERHILoadOp::Clear:		return VK_ATTACHMENT_LOAD_OP_CLEAR;
	case ERHILoadOp::DontCare:	return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	default:					return VK_ATTACHMENT_LOAD_OP_LOAD;
	}
}
inline VkAttachmentStoreOp ToVkStoreOp(ERHIStoreOp op) noexcept
{
	switch (op)
	{
	case ERHIStoreOp::Store:	return VK_ATTACHMENT_STORE_OP_STORE;
	case ERHIStoreOp::DontCare:	return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	default:					return VK_ATTACHMENT_STORE_OP_STORE;
	}
}

// ---- シェーダーステージ ---- //
inline VkShaderStageFlags ToVkShaderStageFlags(ERHIShaderStage s) noexcept
{
	VkShaderStageFlags flags = 0;
	if (HasAnyFlag(s, ERHIShaderStage::Vertex))	flags |= VK_SHADER_STAGE_VERTEX_BIT;
	if (HasAnyFlag(s, ERHIShaderStage::Pixel))	flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
	return flags;
}

// ---- 比較関数 / サンプラー ---- //
inline VkCompareOp ToVkCompareOp(ERHICompareFunc f) noexcept
{
	switch (f)
	{
	case ERHICompareFunc::Never:		return VK_COMPARE_OP_NEVER;
	case ERHICompareFunc::Less:			return VK_COMPARE_OP_LESS;
	case ERHICompareFunc::Equal:		return VK_COMPARE_OP_EQUAL;
	case ERHICompareFunc::LessEqual:	return VK_COMPARE_OP_LESS_OR_EQUAL;
	case ERHICompareFunc::Greater:		return VK_COMPARE_OP_GREATER;
	case ERHICompareFunc::NotEqual:		return VK_COMPARE_OP_NOT_EQUAL;
	case ERHICompareFunc::GreaterEqual:	return VK_COMPARE_OP_GREATER_OR_EQUAL;
	case ERHICompareFunc::Always:		return VK_COMPARE_OP_ALWAYS;
	default:							return VK_COMPARE_OP_LESS;
	}
}

inline VkSamplerAddressMode ToVkAddressMode(ERHIAddressMode m) noexcept
{
	switch (m)
	{
	case ERHIAddressMode::Wrap:		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case ERHIAddressMode::Mirror:	return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case ERHIAddressMode::Clamp:	return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case ERHIAddressMode::Border:	return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	default:						return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

inline VkBorderColor ToVkBorderColor(ERHIBorderColor c) noexcept
{
	switch (c)
	{
	case ERHIBorderColor::TransparentBlack:	return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	case ERHIBorderColor::OpaqueBlack:		return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	case ERHIBorderColor::OpaqueWhite:		return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	default:								return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	}
}

// ---- トポロジー / ラスタライザ / ブレンド ---- //
inline VkPrimitiveTopology ToVkTopology(ERHIPrimitiveTopology t) noexcept
{
	switch (t)
	{
	case ERHIPrimitiveTopology::TriangleList:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	case ERHIPrimitiveTopology::TriangleStrip:	return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	case ERHIPrimitiveTopology::LineList:		return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case ERHIPrimitiveTopology::LineStrip:		return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	case ERHIPrimitiveTopology::PointList:		return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	default:									return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
}

inline VkPolygonMode ToVkPolygonMode(ERHIFillMode m) noexcept
{
	return (m == ERHIFillMode::Wireframe) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
}

inline VkCullModeFlags ToVkCullMode(ERHICullMode m) noexcept
{
	switch (m)
	{
	case ERHICullMode::None:	return VK_CULL_MODE_NONE;
	case ERHICullMode::Front:	return VK_CULL_MODE_FRONT_BIT;
	case ERHICullMode::Back:	return VK_CULL_MODE_BACK_BIT;
	default:					return VK_CULL_MODE_BACK_BIT;
	}
}

inline VkBlendFactor ToVkBlendFactor(ERHIBlendFactor f) noexcept
{
	switch (f)
	{
	case ERHIBlendFactor::Zero:			return VK_BLEND_FACTOR_ZERO;
	case ERHIBlendFactor::One:			return VK_BLEND_FACTOR_ONE;
	case ERHIBlendFactor::SrcColor:		return VK_BLEND_FACTOR_SRC_COLOR;
	case ERHIBlendFactor::InvSrcColor:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case ERHIBlendFactor::SrcAlpha:		return VK_BLEND_FACTOR_SRC_ALPHA;
	case ERHIBlendFactor::InvSrcAlpha:	return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case ERHIBlendFactor::DstColor:		return VK_BLEND_FACTOR_DST_COLOR;
	case ERHIBlendFactor::InvDstColor:	return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case ERHIBlendFactor::DstAlpha:		return VK_BLEND_FACTOR_DST_ALPHA;
	case ERHIBlendFactor::InvDstAlpha:	return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	default:							return VK_BLEND_FACTOR_ONE;
	}
}

inline VkBlendOp ToVkBlendOp(ERHIBlendOp op) noexcept
{
	switch (op)
	{
	case ERHIBlendOp::Add:			return VK_BLEND_OP_ADD;
	case ERHIBlendOp::Subtract:		return VK_BLEND_OP_SUBTRACT;
	case ERHIBlendOp::RevSubtract:	return VK_BLEND_OP_REVERSE_SUBTRACT;
	case ERHIBlendOp::Min:			return VK_BLEND_OP_MIN;
	case ERHIBlendOp::Max:			return VK_BLEND_OP_MAX;
	default:						return VK_BLEND_OP_ADD;
	}
}