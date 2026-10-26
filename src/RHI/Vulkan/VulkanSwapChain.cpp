#include "VulkanSwapChain.hpp"
#include "VulkanDevice.hpp"
#include "Core/Log.hpp"

#include <algorithm>
#include <cassert>

VulkanSwapChain::VulkanSwapChain(VulkanDevice& device)
	: m_Device(device)
{}

VulkanSwapChain::~VulkanSwapChain()
{
	VkDevice dev = m_Device.GetVkDevice();

	// バックバッファの ImageView を先に破棄してから SwapChain を破棄する
	m_BackBuffers.clear();

	if (m_AcquireFence != VNULL)
	{
		vkDestroyFence(dev, m_AcquireFence, nullptr);
		m_AcquireFence = VNULL;
	}

	if (m_SwapChain != VNULL)
	{
		vkDestroySwapchainKHR(dev, m_SwapChain, nullptr);
		m_SwapChain = VNULL;
	}

	if (m_Surface != VNULL)
	{
		vkDestroySurfaceKHR(m_Device.GetVkInstance(), m_Surface, nullptr);
		m_Surface = VNULL;
	}
}

bool VulkanSwapChain::Init(const RHISwapChainDesc& desc)
{
	if (desc.windowHandle == nullptr || desc.width == 0 || desc.height == 0 || desc.bufferCount < 2)
	{
		assert(!"CreateSwapChain: 未対応の RHISwapChainDesc");
		return false;
	}

	m_BufferCount = desc.bufferCount;
	m_Format = desc.format;
	m_Vsync = desc.vSync;

	// Win32 ウィンドウハンドル
	VkWin32SurfaceCreateInfoKHR surfaceInfo{};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.hinstance = GetModuleHandleW(nullptr);
	surfaceInfo.hwnd = static_cast<HWND>(desc.windowHandle);

	if (vkCreateWin32SurfaceKHR(m_Device.GetVkInstance(), &surfaceInfo, nullptr, &m_Surface) != VK_SUCCESS)
	{
		LogError("[RHI] Vulkan SwapChain: サーフェスの作成に失敗しました");
		return false;
	}

	// 選んだGraphicsキューがこのSurfaceへのPresentに対応しているか確認
	VkBool32 presentSupport = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR(
		m_Device.GetVkPhysicalDevice(),
		m_Device.GetGraphicsQueueFamily(),
		m_Surface,
		&presentSupport);
	if (presentSupport != VK_TRUE)
	{
		LogError("[RHI] Vulkan SwapChain: SurfaceへのPresentに対応していないキューファミリーです");
		return false;
	}

	// AcquireNextImage待機用のバイナリfネスを一つ作成
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (vkCreateFence(m_Device.GetVkDevice(), &fenceInfo, nullptr, &m_AcquireFence) != VK_SUCCESS)
	{
		LogError("[RHI] Vulkan SwapChain: AcquireNextImage用フェンスの作成に失敗しました");
		return false;
	}


	return CreateSwapChainInternal(desc.width, desc.height);
}

bool VulkanSwapChain::CreateSwapChainInternal(uint32_t width, uint32_t height)
{
	VkPhysicalDevice physDev = m_Device.GetVkPhysicalDevice();
	VkDevice dev = m_Device.GetVkDevice();

	// surfaceのフォーマットを取得
	VkSurfaceCapabilitiesKHR caps{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, m_Surface, &caps);

	m_Width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
	m_Height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);

	uint32_t imageCount = (std::max(m_BufferCount, caps.minImageCount));
	if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
	{
		imageCount = caps.maxImageCount;
	}

	// 対応フォーマットの選択
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, m_Surface, &formatCount, nullptr);
	if (formatCount == 0) return false;

	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, m_Surface, &formatCount, formats.data());

	VkSurfaceFormatKHR chosenFormat = formats[0];
	const VkFormat wantedVkFormat = ToVkFormat(m_Format);
	for (const VkSurfaceFormatKHR& sf : formats)
	{
		if (sf.format == wantedVkFormat && sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			chosenFormat = sf;
			break;
		}
	}

	// 実際に選ばれたフォーマットを RHI 型へ逆変換して記録
	const ERHIFormat actualFormat = FromVkFormat(chosenFormat.format);
	if (actualFormat != ERHIFormat::Unknown)
	{
		m_Format = actualFormat;
	}

	// Presnetモードの選択
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (!m_Vsync)
	{
		uint32_t modeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, m_Surface, &modeCount, nullptr);
		std::vector<VkPresentModeKHR> modes(modeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, m_Surface, &modeCount, modes.data());
		for (VkPresentModeKHR mode : modes)
		{
			if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR || mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				presentMode = mode;
				break;
			}
		}
	}
	// ---- VkSwapchainKHR の生成 ---- //
	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = m_Surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = chosenFormat.format;
	createInfo.imageColorSpace = chosenFormat.colorSpace;
	createInfo.imageExtent = { m_Width, m_Height };
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.preTransform = caps.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;
	if (vkCreateSwapchainKHR(dev, &createInfo, nullptr, &m_SwapChain) != VK_SUCCESS)
	{
		LogError("[RHI] vkCreateSwapchainKHR に失敗");
		return false;
	}
	return CreateBackBuffers();
}

bool VulkanSwapChain::CreateBackBuffers()
{
	VkDevice dev = m_Device.GetVkDevice();
	// SwapChain が作った VkImage の配列を取得する
	uint32_t count = 0;
	vkGetSwapchainImagesKHR(dev, m_SwapChain, &count, nullptr);
	if (count == 0) return false;
	std::vector<VkImage> images(count);
	vkGetSwapchainImagesKHR(dev, m_SwapChain, &count, images.data());
	m_BufferCount = count;
	m_BackBuffers.clear();
	m_BackBuffers.reserve(m_BufferCount);
	for (uint32_t i = 0; i < m_BufferCount; ++i)
	{
		RHITextureDesc td{};
		td.width = m_Width;
		td.height = m_Height;
		td.format = m_Format;
		td.usage = ERHITextureUsage::RenderTarget;
		// 第4引数 (memory) を省略 (= VK_NULL_HANDLE) することで、VkImage 本体は破棄せず ImageView だけ管理する
		auto texture = std::make_unique<VulkanTexture>(dev, td, images[i]);
		if (!texture->CreateView())
		{
			return false;
		}
		m_BackBuffers.push_back(std::move(texture));
	}
	return true;
}
uint32_t VulkanSwapChain::AcquireNextImage()
{
	VkDevice dev = m_Device.GetVkDevice();
	vkResetFences(dev, 1, &m_AcquireFence);
	// 次に描くバックバッファの番号を取得し、使えるようになるまで Fence で待つ
	uint32_t imageIndex = 0;
	const VkResult res = vkAcquireNextImageKHR(
		dev, m_SwapChain, UINT64_MAX,
		VK_NULL_HANDLE, m_AcquireFence, &imageIndex);
	if (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR)
	{
		vkWaitForFences(dev, 1, &m_AcquireFence, VK_TRUE, UINT64_MAX);
		m_CurrentImageIndex = imageIndex;
	}
	return m_CurrentImageIndex;
}
IRHITexture* VulkanSwapChain::GetBackBuffer(uint32_t index) const
{
	assert(index < m_BackBuffers.size());
	return m_BackBuffers[index].get();
}
ERHIPresentResult VulkanSwapChain::Present()
{
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_SwapChain;
	presentInfo.pImageIndices = &m_CurrentImageIndex;
	const VkResult res = vkQueuePresentKHR(m_Device.GetVkQueue(), &presentInfo);
	if (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR)
	{
		return ERHIPresentResult::Ok;
	}
	if (res == VK_ERROR_OUT_OF_DATE_KHR)
	{
		return ERHIPresentResult::OutOfDate;
	}
	LogError("[RHI] vkQueuePresentKHR に失敗");
	return ERHIPresentResult::Failed;
}
bool VulkanSwapChain::Resize(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0) return false;
	if (width == m_Width && height == m_Height) return true;
	// バックバッファを GPU が使い終わるのを待ってから破棄する
	m_Device.WaitIdle();
	m_BackBuffers.clear();
	if (m_SwapChain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(m_Device.GetVkDevice(), m_SwapChain, nullptr);
		m_SwapChain = VK_NULL_HANDLE;
	}
	return CreateSwapChainInternal(width, height);
}