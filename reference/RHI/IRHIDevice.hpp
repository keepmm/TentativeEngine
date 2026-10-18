/*****************************************************************//**
 * \file   IRHIDevice.hpp
 * \brief  RHI デバイス。 GPU オブジェクトの生成・キュー投入・遅延解放の窓口
 * 
 * 作成者 keepmm
 * 作成日 2026/9/29
 * 更新履歴 9.30 作成
 * memo RHI(Render Hardware Interface
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

/// @brief スレッド規約
/// @brief Create* どのスレッドからでも呼ばれる(AsyncLoader空のロード用)
/// @brief Submit : 1 スレッドからだけ呼ぶ(Vulkan の vkQueueSubmitは外部同期が必要)
class IRHIDevice
{
public:
	virtual ~IRHIDevice() = default;

	IRHIDevice(const IRHIDevice&) = delete;
	IRHIDevice& operator=(const IRHIDevice&) = delete;

	// ---- 情報 ---- //
	virtual ERHIBackend GetBackend() const noexcept = 0;
	virtual const RHIDeviceCaps& GetCaps() const noexcept = 0;

	virtual uint32_t GetFramesInFlight() const noexcept = 0;

	// ---- リソース生成 ---- //

	/// @brief テクスチャを作る。生成直後は常に Undefined なので
	/// @brief 最初に使う前に Transition(Undefined -> 目的の状態) を記録すること
	virtual RHITextureRef CreateTexture(
		_In_ const RHITextureDesc& desc
	) = 0;

	/// @brief バッファを作る
	/// @param initialData Uploadのときだけ有効。生成と同時に書き込む(GpuOnlyへの転送はコマンドリストで)
	/// @return 
	virtual RHIBufferRef CreateBuffer(
		_In_ const RHIBufferDesc& desc,
		_In_opt_ const void* initialData = nullptr
	) = 0;

	/// @brief テクスチャとバッファの間でコピーするときの並びを求める
	/// @param out numSubresources 個ぶん書き込む（firstSubresource から順に）
	/// @return 必要なバッファの合計バイト数。失敗時は 0
	virtual uint64_t GetCopyableFootprints(
		_In_ IRHITexture* texture,
		_In_ uint32_t firstSubresource,
		_In_ uint32_t numSubresources,
		_Out_writes_(numSubresources) RHISubresourceFootprint* out
	) = 0;

	// ---- パイプライン生成 ---- //

	/// @brief ルートシグネチャ / VkPipelineLayoutを作る
	/// @param desc 
	/// @return 
	virtual RHIPipelineLayoutRef CreatePipelineLayout(
		_In_ const RHIPipelineLayoutDesc& desc
	) = 0;

	/// @brief グラフィックス PSO を作る
	virtual RHIPipelineStateRef CreatePipelineState(
		_In_ const RHIGraphicsPipelineDesc& desc
		// 
	) = 0;

	/// @brief layout の TextureTable スロット用のデスクリプタテーブルを作る
	/// @brief 枚数はそのスロットの count。最初は全部空（シェーダーからは 0 が読める）
	virtual RHIDescriptorTableRef CreateDescriptorTable(
		_In_ IRHIPipelineLayout* layout,
		_In_ uint32_t slot
	) = 0;

	// ---- コマンド / 同期 / 表示 ---- //

	/// @brief コマンドリストを作る アロケータ(VkCommandPool)を一つ内包
	virtual RHICommandListRef CreateCommandList(
		_In_opt_ const char* debugName = nullptr
	) = 0;

	/// @brief フェンス
	/// @brief D3D12 : ID3D12Device
	/// @brief Vulkan : timeline semaphore(1.2標準)
	virtual RHIFenceRef CreateFence(
		_In_ const uint64_t initialValue = 0
	) = 0;

	/// @brief スワップチェインを作る
	virtual RHISwapChainRef CreateSwapChain(
		_In_ const RHISwapChainDesc& desc
	) = 0;

	/// @brief コマンドリストをグラフィックキューへ投入する
	/// @param signalFence 非 null なら投入後に signalValueをシグナルする
	virtual void Submit(
		_In_reads_(count) IRHICommandList* const* lists,
		_In_ uint32_t count,
		_In_opt_ IRHIFence* signalFence = nullptr,
		_In_ uint64_t signalValue = 0
	) = 0;

	/// @brief GPUが全作業を終えるまでCPUを止める
	virtual void WaitIdle() = 0;

	// ---- フレーム管理 ---- //

	/// @brief フレームの記録開始を通知
	/// @param slot frameNumber % frameInFlight 呼び出し側はこのスロットの前回分のGPU完了を待ってから呼ぶ 
	virtual void BeginFrame(
		_In_ uint32_t slot
	) = 0;

	// ---- 移行期間用の抜け道 ---- //

	/// @brief ID3D12 : ID3D12Device* / VK : VkDevice
	/// @brief ImGui 初期化や未移行のパス用
	virtual void* GetNativeDevice() const noexcept = 0;

	/// @brief ID3D12 : ID3D12CommandQueue* / VK : VkQueue 
	virtual void* GetNativeQueue() const noexcept = 0;
protected:
	IRHIDevice() = default;
};

/// @brief desc.backendに応じたデバイスを作る
std::unique_ptr<IRHIDevice> CreateRHIDevice(
	_In_ const RHIDeviceDesc& desc
);
