/*****************************************************************//**
 * \file   D3D12Device.hpp
 * \brief  IRHIDeviceの D3D12 実装
 * 
 * 作成者 
 * 作成日 2026/10/13
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "D3D12DescriptorHeap.hpp"
#include "../IRHIDevice.hpp"

#include <atomic>
#include <mutex>

class D3D12Device final : public IRHIDevice
{
public:
	D3D12Device() = default;
	~D3D12Device() override;

	/// @brief アダプタを選んでデバイスとキューを作る
	/// @return 失敗時 false (CreateRHIDevice が nullptr を返す)
	bool Init(_In_ const RHIDeviceDesc& desc);

	// ---- IRHIDevice : 情報 ---- //
	ERHIBackend GetBackend() const noexcept override { return ERHIBackend::D3D12; }
	const RHIDeviceCaps& GetCaps() const noexcept override { return m_Caps; }
	uint32_t GetFramesInFlight() const noexcept override { return m_FrameInFlight; }

	// ---- IRHIDevice : リソース作成 ---- //
	RHITextureRef CreateTexture(_In_ const RHITextureDesc& desc)override;
	RHIBufferRef CreateBuffer(_In_ const RHIBufferDesc& desc, _In_opt_ const void* initialData)override;
	uint64_t GetCopyableFootprints(
		_In_ IRHITexture* texture,
		_In_ uint32_t firstSubresource,
		_In_ uint32_t numSubresources,
		_Out_writes_(numSubresources) RHISubresourceFootprint* out) override;

	// ---- IRHIDevice : パイプライン作成 ---- //
	RHIPipelineLayoutRef CreatePipelineLayout(_In_ const RHIPipelineLayoutDesc& desc) override;
	RHIPipelineStateRef CreatePipelineState(_In_ const RHIGraphicsPipelineDesc& desc) override;
	RHIDescriptorTableRef CreateDescriptorTable(_In_ IRHIPipelineLayout* layout, _In_ uint32_t slot) override;
	RHICommandListRef CreateCommandList(_In_opt_ const char* debugName) override;
	RHIFenceRef CreateFence(_In_ uint64_t initialValue) override;
	RHISwapChainRef CreateSwapChain(_In_ const RHISwapChainDesc& desc) override;
	void Submit(
		_In_reads_(count) IRHICommandList* const* lists,
		_In_ uint32_t count,
		_In_opt_ IRHIFence* signalFence,
		_In_ uint64_t signalValue) override;

	// ---- IRHIDevice : 同期 / フレーム ---- //
	void WaitIdle()override;
	void BeginFrame(_In_ uint32_t slot)override;

	// ---- IRHIDevice : 移行期間用 ---- //
	void* GetNativeDevice() const noexcept override { return m_Device.Get(); }
	void* GetNativeQueue() const noexcept override { return m_Queue.Get(); }

	// ---- D3D12 バックエンド内部用 ---- //
	ID3D12Device* GetD3D12Device() const noexcept { return m_Device.Get(); }
	ID3D12CommandQueue* GetQueue() const noexcept { return m_Queue.Get(); }
	IDXGIFactory4* GetFactory() const noexcept { return m_Factory.Get(); }
	D3D12DescriptorHeap& GetSrvHeap() noexcept { return m_SrvHeap; }
	D3D12DescriptorHeap& GetRtvHeap() noexcept { return m_RtvHeap; }
	D3D12DescriptorHeap& GetDsvHeap() noexcept { return m_DsvHeap; }

	/// @brief デスクリプタテーブルの置き場。コマンドリストは記録の最初にこれを設定する
	D3D12DescriptorHeap& GetShaderVisibleHeap() noexcept { return m_ShaderVisibleHeap; }

	/// @brief 空き枠に置く null SRV（読むと 0 が返る。CPU 専用ヒープ上）
	D3D12_CPU_DESCRIPTOR_HANDLE GetNullSrv() const noexcept { return m_SrvHeap.Cpu(m_NullSrvIndex); }
private:
	static constexpr uint32_t SRV_HEAP_CAPACITY = 4096;
	static constexpr uint32_t RTV_HEAP_CAPACITY = 256;
	static constexpr uint32_t DSV_HEAP_CAPACITY = 64;
	static constexpr uint32_t SHADER_VISIBLE_HEAP_CAPACITY = 16384;

	bool EnableDebugLayer(_In_ bool gpuValidation);
	bool SelectAdapter();
	void SetupInfoQueue();
	void QueryCaps();

	template<class T>
	std::shared_ptr<T> MakeRef(_In_ std::unique_ptr<T> obj)
	{
		++m_LiveObjects;
		return std::shared_ptr<T>(obj.release(), [this](T* p) {DeferDelete(p); });
	}
	void DeferDelete(_In_ IRHIObject* obj);
	void FlushAllPending();

	ComPtr<IDXGIFactory4> m_Factory;
	ComPtr<IDXGIAdapter1> m_Adapter;
	ComPtr<ID3D12Device> m_Device;
	ComPtr<ID3D12CommandQueue> m_Queue;

	// WaitIdle用
	ComPtr<ID3D12Fence> m_IdleFence;
	std::atomic<uint64_t> m_IdleFenceValue{ 0 };

	D3D12DescriptorHeap m_SrvHeap;
	D3D12DescriptorHeap m_RtvHeap;
	D3D12DescriptorHeap m_DsvHeap;
	D3D12DescriptorHeap m_ShaderVisibleHeap;
	uint32_t m_NullSrvIndex = D3D12DescriptorHeap::INVALID_INDEX;

	RHIDeviceCaps m_Caps{};
	uint32_t m_FrameInFlight = 0;
	bool m_DebugLayerEnabled = false;

	// 遅延解放(スロットごと)
	std::mutex m_PendingMutex;
	std::vector<std::vector<std::unique_ptr<IRHIObject>>> m_PendingDelete;
	uint32_t m_CurrentSlot = 0;
	std::atomic<int64_t> m_LiveObjects{ 0 };
};
