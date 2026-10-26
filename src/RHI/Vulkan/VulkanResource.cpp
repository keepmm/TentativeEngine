#include "VulkanResource.hpp"

VulkanTexture::VulkanTexture(
	VkDevice device,
	const RHITextureDesc& desc,
	VkImage image,
	VkDeviceMemory memory)
	: m_Device(device)
	, m_Image(image)
	, m_Memory(memory)
	, m_Desc(desc)
	, m_OwnsImage(memory != VK_NULL_HANDLE)
{
	m_Desc.debugName = nullptr;
}

VulkanTexture::~VulkanTexture()
{
	if (m_View != VNULL)
	{
		vkDestroyImageView(m_Device, m_View, nullptr);
		m_View = VNULL;
	}

	// SwapChainのバックバッファはSwapChain側が所有しているので破棄しない
	if (m_OwnsImage)
	{
		if (m_Image != VNULL)
		{
			vkDestroyImage(m_Device, m_Image, nullptr);
			m_Image = VNULL;
		}
		if (m_Memory != VNULL)
		{
			vkFreeMemory(m_Device, m_Memory,nullptr);
			m_Memory = VNULL;
		}
	}
}

bool VulkanTexture::CreateView()
{
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = m_Image;
	viewInfo.format = ToVkFormat(m_Desc.format);

	switch (m_Desc.dimension)
	{
	case ERHITextureDimension::Texture2D:
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		break;
	case ERHITextureDimension::Texture2DArray:
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		break;
	case ERHITextureDimension::TextureCube:
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		break;
	case ERHITextureDimension::Texture3D:
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
		break;
	}

	// 深度テクスチャならDEPTH_BIT それ以外はCOLOR_BIT
	viewInfo.subresourceRange.aspectMask = IsDepthFormat(m_Desc.format)
		? VK_IMAGE_ASPECT_DEPTH_BIT
		: VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = m_Desc.mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = (m_Desc.dimension == ERHITextureDimension::Texture3D)
		? 1
		: m_Desc.depthOrArraySize;

	return vkCreateImageView(m_Device, &viewInfo, nullptr, &m_View) == VK_SUCCESS;
}

VulkanBuffer::VulkanBuffer(VkDevice device, const RHIBufferDesc& desc, VkBuffer buffer, VkDeviceMemory memory) : m_Device(device), m_Buffer(buffer), m_Memory(memory), m_Desc(desc)
{
	m_Desc.debugName = nullptr;

	if (m_Desc.memory == ERHIMemoryType::Upload)
	{
		// Uploadは D3D12と同じく生成時からマップしっぱなしになる
		if (vkMapMemory(m_Device, m_Memory, 0, m_Desc.size, 0, &m_Persistent) != VK_SUCCESS)
		{
			m_Persistent = nullptr;
		}
	}
}

VulkanBuffer::~VulkanBuffer()
{
	if (m_Persistent != nullptr)
	{
		vkUnmapMemory(m_Device, m_Memory);
		m_Persistent = nullptr;
	}
	if (m_Buffer != VNULL)
	{
		vkDestroyBuffer(m_Device, m_Buffer, nullptr);
		m_Buffer = VNULL;
	}
	if (m_Memory != VNULL)
	{
		vkFreeMemory(m_Device, m_Memory,nullptr);
		m_Memory = VNULL;
	}
}

void* VulkanBuffer::Map()
{
	if (m_Desc.memory == ERHIMemoryType::Upload)
	{
		return m_Persistent;
	}
	if (m_Desc.memory == ERHIMemoryType::ReadBack)
	{
		void* ptr = nullptr;
		if (vkMapMemory(m_Device, m_Memory, 0, m_Desc.size, 0, &ptr) == VK_SUCCESS)
		{
			return ptr;
		}
	}

	return nullptr;
}

void VulkanBuffer::Unmap()
{
	if (m_Desc.memory == ERHIMemoryType::ReadBack)
	{
		vkUnmapMemory(m_Device, m_Memory);
	}
}
