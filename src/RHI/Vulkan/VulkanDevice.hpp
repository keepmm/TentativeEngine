/*****************************************************************//**
 * \file   VulkanDevice.hpp
 * \brief  IRHIDevice の Vulkan実装
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "VulkanTypes.hpp"
#include "../IRHIDevice.hpp"

#include <atomic>
#include <mutex>

class VulkanDevice final : public IRHIDevice
{
public:
	VulkanDevice() = default;
	~VulkanDevice() override;

	bool Init(_In_ const RHIDeviceDesc& desc);

	// ---- IRHIDevice : 情報 ---- //
	ERHIBackend GetBackend() const noexcept override { return ERHIBackend::Vulkan; }
	const RHIDeviceCaps& GetCaps() const noexcept override { return m_Caps; }
	uint32_t GetFramesInFlight() const noexcept override { return m_FrameInFlight; }

	// ---- IRHIDevice : リソース作成 ---- //
	RHITextureRef CreateTexture(_In_ const RHITextureDesc& desc)override;
	RHIBufferRef CreateBuffer(_In_ const RHIBufferDesc& desc, _In_opt_ const void* initialData)override;
	uint64_t GetCopyableFootprints(
		_In_ IRHITexture* texture,
		_In_ uint32_t firstSubresource,
		_In_ uint32_t numSubresources,
		_Out_writes_(numSubresources) RHISubresourceFootprint* out)override;

	// ---- IRHIDevice : パイプライン作成 ---- //
	RHIPipelineLayoutRef CreatePipelineLayout(_In_ const RHIPipelineLayoutDesc& desc)override;
	RHIPipelineStateRef CreatePipelineState(_In_ const RHIGraphicsPipelineDesc& desc)override;
	RHIDescriptorTableRef CreateDescriptorTable(_In_ IRHIPipelineLayout* layout, _In_ uint32_t slot)override;
	RHICommandListRef CreateCommandList(_In_opt_ const char* debugName)override;
	RHIFenceRef CreateFence(_In_ uint64_t initialValue)override;
	RHISwapChainRef CreateSwapChain(_In_ const RHISwapChainDesc& desc)override;

	void Submit(
		_In_reads_(count) IRHICommandList* const* list,
		_In_ uint32_t count,
		_In_opt_ IRHIFence* signalFence,
		_In_ uint64_t singalValue
	)override;

	// ---- IRHIDevice : 同期 / フレーム ---- //
	void WaitIdle() override;
	void BeginFrame(_In_ uint32_t slot)override;

	// ---- IRHIDevice : 移行期間用 ---- //
	void* GetNativeDevice() const noexcept override { return m_Device; }
	void* GetNativeQueue() const noexcept override { return m_Queue; }

	// ---- Vulkanバックエンド用 ---- //
	VkInstance GetVkInstance() const noexcept { return m_Instance; }
	VkPhysicalDevice GetVkPhysicalDevice() const noexcept { return m_PhysicalDevice; }
	VkDevice GetVkDevice() const noexcept { return m_Device; }
	VkQueue GetVkQueue() const noexcept { return m_Queue; }
	uint32_t GetGraphicsQueueFamily() const noexcept { return m_GraphicsQueueFamily; }
private:
	bool CreateInstance(_In_ bool enableBebugLayer);
	bool SelectPhysicalDevice();
	bool FindQueueFamilies();
	bool CreateLogicalDevice();
	void QueryCaps();

	uint32_t FindMemoryType(_In_ uint32_t typeBits, _In_ VkMemoryPropertyFlags properties)const;

	template<class T>
	std::shared_ptr<T> MakeRef(_In_ std::unique_ptr<T> obj)
	{
		++m_LiveObjects;
		return std::shared_ptr<T>(obj.release(), [this](T* p) {DeferDelete(p); });
	}
	void DeferDelete(_In_ IRHIObject* obj);
	void FlushAllPending();

	VkInstance m_Instance				= VK_NULL_HANDLE;
	VkPhysicalDevice m_PhysicalDevice	= VK_NULL_HANDLE;
	VkDevice m_Device					= VK_NULL_HANDLE;
	VkQueue m_Queue						= VK_NULL_HANDLE;
	uint32_t m_GraphicsQueueFamily		= UINT32_MAX;
	VkDescriptorPool m_DescriptorPool   = VK_NULL_HANDLE;

	RHIDeviceCaps m_Caps{};
	uint32_t m_FrameInFlight = 0;
	bool m_DebuglayerEnabled = false;

	// ---- フレームごとの遅延解放キュー ---- //
	std::mutex m_PendingMutex;
	uint32_t m_CurrentSlot = 0;
	std::vector<std::vector<std::unique_ptr<IRHIObject>>> m_PendingDelete;
	std::atomic<int64_t> m_LiveObjects{ 0 };
};
