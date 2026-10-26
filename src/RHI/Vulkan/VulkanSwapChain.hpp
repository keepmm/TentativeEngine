/*****************************************************************//**
 * \file   VulkanSwapChain.hpp
 * \brief  IRHISwapChainのVulkan実装
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "VulkanTypes.hpp"
#include "VulkanResource.hpp"
#include "../IRHISwapChain.hpp"

class VulkanDevice;

class VulkanSwapChain final : public IRHISwapChain
{
public:
	explicit VulkanSwapChain(_In_ VulkanDevice& device);
	~VulkanSwapChain() override;

	/// @brief サーフェスとスワップチェインを作る
	/// @return 失敗時 false
	bool Init(_In_ const RHISwapChainDesc& desc);

	// ---- IRHISwapChain ---- //
	uint32_t AcquireNextImage() override;
	IRHITexture* GetBackBuffer(_In_ uint32_t index) const override;
	ERHIPresentResult Present() override;
	bool Resize(_In_ uint32_t width, _In_ uint32_t height) override;

	uint32_t GetBufferCount() const noexcept override { return m_BufferCount; }
	uint32_t GetWidth() const noexcept override { return m_Width; }
	uint32_t GetHeight() const noexcept override { return m_Height; }
	ERHIFormat GetFormat() const noexcept override { return m_Format; }
	void* GetNativeSwapChain() const noexcept override { return m_SwapChain; }
private:
	bool CreateSwapChainInternal(_In_ uint32_t width, _In_ uint32_t height);
	bool CreateBackBuffers();

	VulkanDevice& m_Device;
	VkSurfaceKHR m_Surface = VNULL;
	VkSwapchainKHR m_SwapChain = VNULL;
	VkFence m_AcquireFence = VNULL;
	std::vector<std::unique_ptr<VulkanTexture>> m_BackBuffers;

	uint32_t m_BufferCount = 0;
	uint32_t m_Width = 0;
	uint32_t m_Height = 0;
	uint32_t m_CurrentImageIndex = 0;
	ERHIFormat m_Format = ERHIFormat::Unknown;
	bool m_Vsync = true;
};
