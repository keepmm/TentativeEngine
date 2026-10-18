/*****************************************************************//**
 * \file   RHIUtil.cpp
 * \brief  RHI のインターフェースだけで書ける便利関数（バックエンドに依存しない）
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成（テクスチャのアップロード）
 * *********************************************************************/
#include "RHIUtil.hpp"
#include "IRHIDevice.hpp"
#include "IRHIBuffer.hpp"
#include "IRHICommandList.hpp"

#include <cstring>

RHIBufferRef RHIUploadTexture(
	IRHIDevice& device,
	IRHICommandList& commandList,
	IRHITexture* texture,
	uint32_t firstSubresource,
	uint32_t count,
	const RHISubresourceData* subresources)
{
	if (texture == nullptr || count == 0 || subresources == nullptr) return nullptr;

	// 行の間隔などの決まりは API ごとに違うので、デバイスに並びを決めてもらう
	std::vector<RHISubresourceFootprint> footprints(count);
	const uint64_t totalSize = device.GetCopyableFootprints(texture, firstSubresource, count, footprints.data());
	if (totalSize == 0) return nullptr;

	RHIBufferDesc desc;
	desc.size		= totalSize;
	desc.usage		= ERHIBufferUsage::CopySrc;
	desc.memory		= ERHIMemoryType::Upload;
	desc.debugName	= "TextureUpload";
	RHIBufferRef staging = device.CreateBuffer(desc);
	if (!staging) return nullptr;

	auto* mapped = static_cast<uint8_t*>(staging->Map());
	if (mapped == nullptr) return nullptr;

	for (uint32_t i = 0; i < count; ++i)
	{
		const RHISubresourceFootprint&	fp	= footprints[i];
		const RHISubresourceData&		src	= subresources[i];

		// 1 行ずつ詰め直す（バッファ側の行の間隔は rowPitch、元データの間隔は src.rowPitch）
		for (uint32_t z = 0; z < fp.depth; ++z)
		{
			const auto*	srcSlice = static_cast<const uint8_t*>(src.data) + src.slicePitch * z;
			uint8_t*	dstSlice = mapped + fp.offset + static_cast<uint64_t>(fp.rowPitch) * fp.rowCount * z;
			for (uint32_t y = 0; y < fp.rowCount; ++y)
			{
				std::memcpy(
					dstSlice + static_cast<uint64_t>(fp.rowPitch) * y,
					srcSlice + src.rowPitch * y,
					fp.rowSize);
			}
		}

		commandList.CopyBufferToTexture(texture, firstSubresource + i, staging.get(), fp);
	}

	staging->Unmap();
	return staging;
}
