/*****************************************************************//**
 * \file   VulkanResource.hpp
 * \brief  IRHITexture / IRHIBufferの Vulkan 実装
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "VulkanTypes.hpp"
#include "../IRHITexture.hpp"
#include "../IRHIBuffer.hpp"

class VulkanTexture : public IRHITexture
{
public:
	VulkanTexture(
		_In_ VkDevice device,
		_In_ const RHITextureDesc& desc,
		_In_ VkImage image,
		_In_ VkDeviceMemory memory = VNULL
	);
	~VulkanTexture() override;

	bool CreateView();

	const RHITextureDesc& GetDesc() const noexcept override { return m_Desc; }
	void* GetNativeResource() const noexcept override { return m_Image; }

	// ---- Vulkanバックエンド用 ---- //
	VkImage GetImage() const noexcept { return m_Image; }
	VkImageView GetVkImageView() const noexcept { return m_View; }

private:
	VkDevice m_Device = VNULL;
	VkImage m_Image = VNULL;
	VkDeviceMemory m_Memory = VNULL;
	VkImageView m_View = VNULL;
	RHITextureDesc m_Desc{};
	bool m_OwnsImage = false;
};

class VulkanBuffer final : public IRHIBuffer
{
public:
	VulkanBuffer(
		_In_ VkDevice device,
		_In_ const RHIBufferDesc& desc,
		_In_ VkBuffer buffer,
		_In_ VkDeviceMemory memory
	);

	~VulkanBuffer() override;

	const RHIBufferDesc& GetDesc() const noexcept override { return m_Desc; }

	void* Map()override;
	void Unmap() override;
	void* GetNativeResource() const noexcept override { return m_Buffer; }

	// ---- Vulkan バックエンド用 ---- //
	VkBuffer GetVkBuffer() const noexcept { return m_Buffer; }
private:
	VkDevice m_Device = VNULL;
	VkBuffer m_Buffer = VNULL;
	VkDeviceMemory m_Memory = VNULL;
	RHIBufferDesc m_Desc{};
	void* m_Persistent = nullptr;
};
