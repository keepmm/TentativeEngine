/*****************************************************************//**
 * \file   IRHICommandList.hpp
 * \brief  RHI コマンドリスト(GPUに対してコマンドを送る)
 * 
 * 作成者 
 * 作成日 2026/10/14
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

// ---- レンダーパス ---- //
enum class ERHILoadOp : uint8_t
{
	Load,	// 前の内容を残す
	Clear,	// クリア値で塗る
	DontCare// 全部上書きするので前の内容はいらない
};

enum class ERHIStoreOp : uint8_t
{
	Store,
	DontCare
};

struct RHIColorAttachment
{
	IRHITexture* texture = nullptr;
	ERHILoadOp loadOp = ERHILoadOp::Load;
	ERHIStoreOp storeOp = ERHIStoreOp::Store;
	float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct RHIDepthAttachment
{
	IRHITexture* texture = nullptr;
	ERHILoadOp loadOp = ERHILoadOp::Load;
	ERHIStoreOp storeOp = ERHIStoreOp::Store;
	float clearDepth = 1.0f;
	uint8_t clearStencil = 0;
};

/// @brief 書き込み際の組
struct RHIRenderPassDesc
{
	RHIColorAttachment colors[RHI_MAX_RENDER_TARGETS]{};
	uint32_t numColors = 0;
	RHIDepthAttachment depth{};
};

// ---- コマンドリスト ---- //
class IRHICommandList : public IRHIObject
{
public:
	// ---- 記録の開始 / 終了 ---- //

	/// @brief 記録を始める
	virtual void Begin() = 0;

	/// @brief 記録を終える
	virtual void End() = 0;

	virtual void Transition(_In_ IRHITexture* texture, _In_ ERHIResourceState before, _In_ ERHIResourceState after) = 0;
	virtual void Transition(_In_ IRHIBuffer* buffer, _In_ ERHIResourceState before, _In_ ERHIResourceState after) = 0;

	// ---- レンダーパス ---- //

	virtual void BeginRenderPass(_In_ const RHIRenderPassDesc& desc) = 0;
	virtual void EndRenderPass() = 0;

	virtual void SetViewport(_In_ const RHIViewport& viewport) = 0;
	virtual void SetScissor(_In_ const RHIRect& rect) = 0;

	// ---- コピー（レンダーパスの外で）---- //

	/// @brief src は CopySrc（Upload は GenericRead のままで可）、dst は CopyDst 状態にしておく
	virtual void CopyBuffer(
		_In_ IRHIBuffer* dst, _In_ uint64_t dstOffset,
		_In_ IRHIBuffer* src, _In_ uint64_t srcOffset,
		_In_ uint64_t size) = 0;

	/// @brief バッファ → テクスチャ（アップロード）。footprint は IRHIDevice::GetCopyableFootprints で求める
	/// @note  dst は CopyDst、src は CopySrc（Upload は GenericRead のままで可）にしておく
	virtual void CopyBufferToTexture(
		_In_ IRHITexture* dst, _In_ uint32_t dstSubresource,
		_In_ IRHIBuffer* src, _In_ const RHISubresourceFootprint& footprint) = 0;

	/// @brief テクスチャ → バッファ（読み戻し）
	/// @note  src は CopySrc、dst は CopyDst（ReadBack はそのままで可）にしておく
	virtual void CopyTextureToBuffer(
		_In_ IRHIBuffer* dst, _In_ const RHISubresourceFootprint& footprint,
		_In_ IRHITexture* src, _In_ uint32_t srcSubresource) = 0;

	// ---- パイプライン / バインド ---- //

	/// @brief PSO を設定する。ルートシグネチャとトポロジーもここで設定される
	/// @note  レイアウトが前の PSO と違うと、それまでのバインドは消える。バインドは PSO の後で
	virtual void SetPipelineState(_In_ IRHIPipelineState* pipeline) = 0;

	/// @brief ConstantBuffer スロットに定数バッファを結び付ける
	/// @param offset 256 の倍数（大きなバッファの一部を使うとき用）
	virtual void SetConstantBuffer(_In_ uint32_t slot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset = 0) = 0;

	/// @brief StructuredBuffer スロットにバッファを結び付ける（ボーン行列など）
	virtual void SetStructuredBuffer(_In_ uint32_t slot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset = 0) = 0;

	/// @brief 頂点バッファ。ストライドは PSO の vertexBuffers から取る
	virtual void SetVertexBuffer(_In_ uint32_t bufferSlot, _In_ IRHIBuffer* buffer, _In_ uint64_t offset = 0) = 0;

	/// @brief インデックスバッファ。format は R16_UINT か R32_UINT
	virtual void SetIndexBuffer(_In_ IRHIBuffer* buffer, _In_ ERHIFormat format, _In_ uint64_t offset = 0) = 0;

	/// @brief TextureTable スロットにデスクリプタテーブルを結び付ける
	virtual void SetDescriptorTable(_In_ uint32_t slot, _In_ IRHIDescriptorTable* table) = 0;

	// ---- 描画（レンダーパスの中で）---- //

	virtual void Draw(
		_In_ uint32_t vertexCount, _In_ uint32_t instanceCount = 1,
		_In_ uint32_t firstVertex = 0, _In_ uint32_t firstInstance = 0) = 0;

	virtual void DrawIndexed(
		_In_ uint32_t indexCount, _In_ uint32_t instanceCount = 1,
		_In_ uint32_t firstIndex = 0, _In_ int32_t baseVertex = 0,
		_In_ uint32_t firstInstance = 0) = 0;

	// ---- 移行期間用 ---- //

	/// @brief D3D12: ID3D12GraphicsCommandList* / VK: VkCommandBuffer
	/// @note  未移行のパスをこのリストに記録するための抜け道
	virtual void* GetNativeCommandList() const noexcept = 0;
};
