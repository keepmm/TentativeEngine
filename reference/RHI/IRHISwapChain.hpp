/*****************************************************************//**
 * \file   IRHISwapChain.hpp
 * \brief  RHI スワップチェイン
 *         D3D12: IDXGISwapChain3 / Vulkan: VkSwapchainKHR
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

enum class ERHIPresentResult : uint8_t
{
	Ok,			// 表示した（最小化などで見えていない場合も含む）
	OutOfDate,	// ウィンドウとサイズが合わなくなった（主に Vulkan）。Resize してから続ける
	Failed,		// デバイスロストなど。続けられない
};

/// @brief 1 フレームの流れ（Vulkan でもこの順番なら成り立つ）
///        1. AcquireNextImage() で今回描くバックバッファの番号を受け取る
///        2. そのバックバッファへ描くコマンドを記録して Submit する
///        3. Present()
/// @note  Vulkan はバックバッファの返ってくる順番が保証されない。
///        番号を予測して、Acquire より前に記録してはいけない
class IRHISwapChain : public IRHIObject
{
public:
	/// @brief 今回描くバックバッファの番号を受け取る
	virtual uint32_t AcquireNextImage() = 0;

	/// @brief バックバッファ。持ち主はスワップチェインで、Resize すると無効になる
	/// @note  毎フレーム Undefined → RenderTarget → Present と遷移させる
	///        （前の内容は使わないので Undefined から。Vulkan の初回もこれで通る）
	virtual IRHITexture* GetBackBuffer(_In_ uint32_t index) const = 0;

	virtual ERHIPresentResult Present() = 0;

	/// @brief バッファを作り直す。中で GPU の完了を待つ
	virtual bool Resize(_In_ uint32_t width, _In_ uint32_t height) = 0;

	virtual uint32_t GetBufferCount() const noexcept = 0;
	virtual uint32_t GetWidth() const noexcept = 0;
	virtual uint32_t GetHeight() const noexcept = 0;

	/// @brief 実際のフォーマット。バックバッファへ描く PSO の rtvFormat にはこれを使う
	virtual ERHIFormat GetFormat() const noexcept = 0;

	/// @brief D3D12: IDXGISwapChain3* / VK: VkSwapchainKHR
	virtual void* GetNativeSwapChain() const noexcept = 0;
};
