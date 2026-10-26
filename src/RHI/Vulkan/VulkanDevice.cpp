#include "VulkanDevice.hpp"
#include "VulkanFence.hpp"
#include "VulkanCommandList.hpp"
#include "VulkanResource.hpp"
#include "VulkanSwapChain.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanDescriptorTable.hpp"
#include "Core/Log.hpp"

#include <cassert>
#include <cstring>
#include <algorithm>

namespace
{
	constexpr uint32_t VENDOR_NVIDIA = 0x10DE;
	constexpr const char* VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";

	bool HasValidationLayerSupport()
	{
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		if (layerCount == 0)return false;

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const VkLayerProperties& layer : availableLayers)
		{
			if (std::strcmp(layer.layerName, VALIDATION_LAYER_NAME) == 0)
			{
				return true;
			}
		}

		return false;
	}
}

VulkanDevice::~VulkanDevice()
{
	// GPUの処理待ちをしてからデバイスを破棄する
	if (m_Device != VK_NULL_HANDLE)
	{
		WaitIdle();
	}

	// デバイス破棄前に全てのRHIオブジェクトを解放する
	FlushAllPending();
	assert(m_LiveObjects.load() == 0 && "RHIオブジェクトがデバイスより長生きしている");
	if (m_DescriptorPool != VNULL)
	{
		vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
		m_DescriptorPool = VNULL;
	}


	// デバイス破棄
	if(m_Device != VK_NULL_HANDLE)
	{
		vkDestroyDevice(m_Device, nullptr);
		m_Device = VK_NULL_HANDLE;
	}

	// インスタンス破棄
	if (m_Instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(m_Instance, nullptr);
		m_Instance = VK_NULL_HANDLE;
	}
}

bool VulkanDevice::Init(const RHIDeviceDesc& desc)
{
	assert(desc.frameInFlight > 0 && "RHIDeviceDesc::frameInFlightにRTV_NUMを渡すこと");
	if (desc.frameInFlight == 0)return false;

	// フレーム数の設定
	m_FrameInFlight = desc.frameInFlight;
	m_PendingDelete.resize(m_FrameInFlight);

	// インスタンスの作成
	if (!CreateInstance(desc.enableDebugLayer))
	{
		LogError("[RHI] Vulkan インスタンスの作成に失敗しました");
		return false;
	}

	// 物理デバイスの選択
	if (!SelectPhysicalDevice())
	{
		LogError("[RHI] Vulkan が使える GPUが見つかりません");
		return false;
	}

	// キューファミリーの検索
	if (!FindQueueFamilies())
	{
		LogError("[RHI] Graphics キューファミリーが見つかりません");
		return false;
	}

	// 論理デバイスとキューの作成
	if (!CreateLogicalDevice())
	{
		LogError("[RHI] Vulkan 論理デバイスの作成に失敗しました");
		return false;
	}

	// ディスクリプタテーブル用のプール作成
	VkDescriptorPoolSize poolSizes[] =
	{
		{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4096},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1024},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1024},
		{VK_DESCRIPTOR_TYPE_SAMPLER,256},
	};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 2048;
	poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
	poolInfo.pPoolSizes = poolSizes;

	if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
	{
		LogError("[RHI] VkDescriptorPool の作成に失敗しました");
		return false;
	}

	// 機能情報の取得
	QueryCaps();
	return true;
}

bool VulkanDevice::CreateInstance(bool enableBebugLayer)
{
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "ModelViewer";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "CustomEngine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	// ウィンドウ表示に必要な Surface 拡張
	std::vector<const char*> extensions =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};

	// デバッグレイヤーの確認と追加
	std::vector<const char*> layers;
	if (enableBebugLayer && HasValidationLayerSupport())
	{
		m_DebuglayerEnabled = true;
		layers.push_back(VALIDATION_LAYER_NAME);
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	// インスタンス生成
	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
	createInfo.ppEnabledLayerNames = layers.data();

	return vkCreateInstance(&createInfo, nullptr, &m_Instance) == VK_SUCCESS;
}

bool VulkanDevice::SelectPhysicalDevice()
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
	if (deviceCount == 0)return false;

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

	// ---- NVIDIA のディスクリート GPU を最優先で探す ---- //
	for (VkPhysicalDevice dev : devices)
	{
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(dev, &props);

		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
			props.vendorID == VENDOR_NVIDIA)
		{
			m_PhysicalDevice = dev;
			return true;
		}
	}

	// ---- それ以外のディスクリートGPUを探す ---- //
	for (VkPhysicalDevice dev : devices)
	{
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(dev, &props);

		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			m_PhysicalDevice = dev;
			return true;
		}
	}

	// なければ先頭のデバイスを使う
	m_PhysicalDevice = devices.front();
	return true;
}

bool VulkanDevice::FindQueueFamilies()
{
	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &familyCount, nullptr);
	if (familyCount == 0)return false;

	std::vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &familyCount, families.data());

	for (uint32_t i = 0; i < familyCount; ++i)
	{
		// Graphicsコマンドを流れるキューファミリーを探す
		if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			m_GraphicsQueueFamily = i;
			return true;
		}
	}

	return false;
}

bool VulkanDevice::CreateLogicalDevice()
{
	const float queuePriority = 1.0f;

	VkDeviceQueueCreateInfo queueInfo{};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = m_GraphicsQueueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &queuePriority;
	
	// スワップチェイン用のデバイス拡張
	const char* deviceExtensions[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	// 基本機能(異方性フィルタリングなど)
	VkPhysicalDeviceFeatures features{};
	features.samplerAnisotropy = VK_TRUE;
	features.fillModeNonSolid = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.timelineSemaphore = VK_TRUE;

	// Vulkan 1.3 機能
	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.pNext = &features12;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &features13;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueInfo;
	createInfo.pEnabledFeatures = &features;
	createInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(deviceExtensions));
	createInfo.ppEnabledExtensionNames = deviceExtensions;

	if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS)
	{
		return false;
	}

	// 0番目のキューを取得
	vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_Queue);
	return true;
}

void VulkanDevice::QueryCaps()
{
	VkPhysicalDeviceProperties props{};
	vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);

	m_Caps.adapterName = props.deviceName;
	m_Caps.constantBufferAlignment = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);
	m_Caps.maxTextureSize2D = props.limits.maxImageDimension2D;

	// VRAM容量を調べる
	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);
	uint64_t vramBytes = 0;
	for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
	{
		if ((memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
		{
			vramBytes += memProps.memoryHeaps[i].size;
		}
	}

	LogInfo("[RHI] Vulkan アダプタ: " + m_Caps.adapterName +
		" (VRAM " + std::to_string(vramBytes / (1024 * 1024)) + " MB)");
}

uint32_t VulkanDevice::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
	VkPhysicalDeviceMemoryProperties memProps{};
	vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		// リソースが要求するビット (typeBits) に含まれており、かつ必要な属性 (properties) を全て満たすか
		if ((typeBits & (1u << i)) != 0 &&
			(memProps.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	return UINT32_MAX;
}

void VulkanDevice::WaitIdle()
{
	if (m_Device != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(m_Device);
	}
}
void VulkanDevice::BeginFrame(uint32_t slot)
{
	assert(slot < m_FrameInFlight);
	m_CurrentSlot = slot;
	// このスロットは呼び出し側で Fence Wait 済みなので、古いリソースを安全に破棄できる
	std::vector<std::unique_ptr<IRHIObject>> dead;
	{
		std::lock_guard<std::mutex> lock(m_PendingMutex);
		dead.swap(m_PendingDelete[slot]);
	}
	m_LiveObjects -= static_cast<int64_t>(dead.size());
	dead.clear();
}
void VulkanDevice::DeferDelete(IRHIObject* obj)
{
	std::lock_guard<std::mutex> lock(m_PendingMutex);
	m_PendingDelete[m_CurrentSlot].emplace_back(obj);
}
void VulkanDevice::FlushAllPending()
{
	for (;;)
	{
		std::vector<std::unique_ptr<IRHIObject>> dead;
		{
			std::lock_guard<std::mutex> lock(m_PendingMutex);
			for (auto& list : m_PendingDelete)
			{
				for (auto& obj : list) dead.push_back(std::move(obj));
				list.clear();
			}
		}
		if (dead.empty()) break;
		m_LiveObjects -= static_cast<int64_t>(dead.size());
		dead.clear();
	}
}

RHITextureRef VulkanDevice::CreateTexture(const RHITextureDesc& desc)
{
	const bool wantsRT = HasAnyFlag(desc.usage, ERHITextureUsage::RenderTarget);
	const bool wantsDS = HasAnyFlag(desc.usage, ERHITextureUsage::DepthStencil);

	if (desc.sampleCount != 1 ||
		desc.format == ERHIFormat::Unknown ||
		wantsDS != IsDepthFormat(desc.format) ||
		(desc.dimension == ERHITextureDimension::TextureCube && desc.depthOrArraySize % 6 != 0) ||
		(desc.dimension == ERHITextureDimension::Texture3D && (wantsRT || wantsDS)))
	{
		assert(!"CreateTexture : 未対応の RHITextureDesc");
		return nullptr;
	}

	// VkImageの作成
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = (desc.dimension == ERHITextureDimension::Texture3D)
		? VK_IMAGE_TYPE_3D
		: VK_IMAGE_TYPE_2D;
	imageInfo.format = ToVkFormat(desc.format);
	imageInfo.extent.width = desc.width;
	imageInfo.extent.height = desc.height;
	imageInfo.extent.depth = (desc.dimension == ERHITextureDimension::Texture3D) ?
		desc.depthOrArraySize : 1;
	imageInfo.mipLevels = desc.mipLevels;
	imageInfo.arrayLayers = (desc.dimension == ERHITextureDimension::Texture3D) ? 1 :
		desc.depthOrArraySize;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = ToVkImageUsage(desc.usage);
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (desc.dimension == ERHITextureDimension::TextureCube)
	{
		imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}

	VkImage image = VNULL;
	if (vkCreateImage(m_Device, &imageInfo, nullptr, &image) != VK_SUCCESS)
	{
		return nullptr;
	}

	// メモリ要件の取得とVRAM確保
	VkMemoryRequirements memReq{};
	vkGetImageMemoryRequirements(m_Device, image, &memReq);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkDeviceMemory memory = VNULL;
	if(allocInfo.memoryTypeIndex == UINT32_MAX ||
		vkAllocateMemory(m_Device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
	{
		vkDestroyImage(m_Device, image, nullptr);
		return nullptr;
	}

	// 器とメモリをバインド
	vkBindImageMemory(m_Device, image, memory, 0);

	// ImageViewの作成
	auto texture = std::make_unique<VulkanTexture>(m_Device, desc, image, memory);
	if (!texture->CreateView())
	{
		return nullptr;
	}
	return MakeRef(std::move(texture));
}
RHIBufferRef VulkanDevice::CreateBuffer(const RHIBufferDesc& desc, const void* initialData)
{
	if (desc.size == 0 ||
		(initialData != nullptr && desc.memory != ERHIMemoryType::Upload) ||
		(HasAnyFlag(desc.usage, ERHIBufferUsage::UnorderedAccess) &&
			desc.memory != ERHIMemoryType::GpuOnly))
	{
		assert(!"Createbuffer: 未対応の RHIBufferDesc");
		return nullptr;
	}

	RHIBufferDesc actual = desc;
	if (HasAnyFlag(desc.usage, ERHIBufferUsage::ConstantBuffer))
	{
		const uint64_t a = (std::max)(uint64_t(256), uint64_t(m_Caps.constantBufferAlignment));
		actual.size = (desc.size + a - 1) & ~(a - 1);
	}

	// VkBufferの作成
	VkBufferCreateInfo bufInfo{};
	bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size = actual.size;
	bufInfo.usage = ToVkBufferUsage(desc.usage);
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer vkBuf = VNULL;

	if(vkCreateBuffer(m_Device, &bufInfo, nullptr, &vkBuf) != VK_SUCCESS)
	{
		return nullptr;
	}
	
	// メモリ要件の取得とメモリ確保
	VkMemoryRequirements memReq{};
	vkGetBufferMemoryRequirements(m_Device, vkBuf, &memReq);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, ToVkMemoryFlags(desc.memory));

	VkDeviceMemory memory = VNULL;
	if (allocInfo.memoryTypeIndex == UINT32_MAX ||
		vkAllocateMemory(m_Device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
	{
		vkDestroyBuffer(m_Device, vkBuf, nullptr);
		return nullptr;
	}

	// 器とメモリをバインド
	vkBindBufferMemory(m_Device, vkBuf, memory, 0);

	auto buffer = std::make_unique<VulkanBuffer>(m_Device, actual, vkBuf, memory);
	if (initialData != nullptr)
	{
		void* dst = buffer->Map();
		if (dst == nullptr)return nullptr;
		std::memcpy(dst, initialData, static_cast<size_t>(desc.size));
	}
	return MakeRef(std::move(buffer));
}

uint64_t VulkanDevice::GetCopyableFootprints(
	IRHITexture* texture,
	uint32_t firstSubresource,
	uint32_t numSubresources,
	RHISubresourceFootprint* out)
{
	if (texture == nullptr || numSubresources == 0 || out == nullptr)return 0;

	const RHITextureDesc& td = texture->GetDesc();
	const uint32_t bpp = GetFormatByteSize(td.format);
	const uint32_t blockSize = IsCompressedFormat(td.format) ? 4u : 1u;

	uint64_t totalBytes = 0;
	for (uint32_t i = 0; i < numSubresources; ++i)
	{
		const uint32_t sub = firstSubresource + i;
		const uint32_t mip = sub % td.mipLevels;

		const uint32_t w = (std::max)(1u, td.width >> mip);
		const uint32_t h = (std::max)(1u, td.height >> mip);
		const uint32_t d = (td.dimension == ERHITextureDimension::Texture3D)
			? (std::max)(1u, td.depthOrArraySize >> mip) : 1u;

		const uint32_t blocksX = (w + blockSize - 1) / blockSize;
		const uint32_t blocksY = (h + blockSize - 1) / blockSize;
		const uint32_t rowSize = blocksX * bpp;
		const uint32_t rowPitch = (rowSize + 255u) & ~255u;

		// バッファオフセットは 512 バイト境界にそろえる
		totalBytes = (totalBytes + 511ull) & ~511ull;

		out[i].offset = totalBytes;
		out[i].rowPitch = rowPitch;
		out[i].rowSize = rowSize;
		out[i].rowCount = blocksY;
		out[i].width = w;
		out[i].height = h;
		out[i].depth = d;

		totalBytes += static_cast<uint64_t>(rowPitch) * blocksY * d;
	}
	return totalBytes;
}
RHIPipelineLayoutRef VulkanDevice::CreatePipelineLayout(const RHIPipelineLayoutDesc& desc)
{
	auto layout = VulkanPipelineLayout::Create(m_Device, desc);
	if (!layout)return nullptr;
	return MakeRef(std::move(layout));
}
RHIPipelineStateRef VulkanDevice::CreatePipelineState(const RHIGraphicsPipelineDesc& desc)
{
	auto pso = VulkanPipelineState::Create(m_Device, desc);
	if (!pso)return nullptr;
	return MakeRef(std::move(pso));
}
RHIDescriptorTableRef VulkanDevice::CreateDescriptorTable(IRHIPipelineLayout* layout, uint32_t slot)
{
	if (layout == nullptr || slot >= layout->GetSlotCount() ||
		layout->GetSlotType(slot) != ERHIBindingType::TextureTable)
	{
		assert(!"CreateDescriptorTable: TextureTable スロットではない");
		return nullptr;
	}

	auto* vkLayout = static_cast<VulkanPipelineLayout*>(layout);
	VkDescriptorSetLayout setLayout = vkLayout->GetVkSetLayout(slot);

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_DescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &setLayout;
	VkDescriptorSet set = VNULL;
	if (vkAllocateDescriptorSets(m_Device, &allocInfo, &set) != VK_SUCCESS)
	{
		LogError("[RHI] VkDescriptorSet の割り当てに失敗しました");
		return nullptr;
	}
	const RHIBindingSlot& slotInfo = layout->GetSlot(slot);
	return MakeRef(std::make_unique<VulkanDescriptorTable>(
		*this, m_DescriptorPool, set, slotInfo.shaderRegister, slotInfo.count));
}
RHICommandListRef VulkanDevice::CreateCommandList(const char* debugName)
{
	(void)debugName;
	
	// コマンドプールを作成
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

	VkCommandPool pool = VNULL;
	if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
	{
		return nullptr;
	}

	// コマンドバッファを一本割り当てる
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = pool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer cmd = VNULL;
	if (vkAllocateCommandBuffers(m_Device, &allocInfo, &cmd) != VK_SUCCESS)
	{
		return nullptr;
	}

	return MakeRef(std::make_unique<VulkanCommandList>(m_Device, pool, cmd));
}
RHIFenceRef VulkanDevice::CreateFence(uint64_t initialValue)
{
	VkSemaphoreTypeCreateInfo typeInfo{};
	typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	typeInfo.initialValue = initialValue;

	VkSemaphoreCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	createInfo.pNext = &typeInfo;

	VkSemaphore semaphore = VNULL;
	if (vkQueueWaitIdle != nullptr &&
		vkCreateSemaphore(m_Device, &createInfo, nullptr, &semaphore) != VK_SUCCESS)
	{
		return nullptr;
	}

	return MakeRef(std::make_unique<VulkanFence>(m_Device, semaphore));
}
RHISwapChainRef VulkanDevice::CreateSwapChain(const RHISwapChainDesc& desc)
{
	auto swapChain = std::make_unique<VulkanSwapChain>(*this);
	if (!swapChain->Init(desc)) return nullptr;
	return MakeRef(std::move(swapChain));
}
void VulkanDevice::Submit(
	IRHICommandList* const* lists,
	uint32_t count,
	IRHIFence* signalFence,
	uint64_t signalValue)
{
	constexpr uint32_t MAX_LISTS = 16;
	assert(count <= MAX_LISTS);

	VkCommandBufferSubmitInfo cmdInfos[MAX_LISTS]{};
	for (uint32_t i = 0; i < count; ++i)
	{
		auto* list = static_cast<VulkanCommandList*>(lists[i]);
		assert(list->IsClosed() && "EndしていないコマンドリストをSubmitした");

		cmdInfos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		cmdInfos[i].commandBuffer = list->GetVkCommandBuffer();
	}

	// signalするフェンス(semaphoreの設定)
	VkSemaphoreSubmitInfo signalInfo{};
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.value = signalValue;
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	if (signalFence != nullptr)
	{
		signalInfo.semaphore = static_cast<VulkanFence*>(signalFence)->GetVkSemaphore();
	}

	VkSubmitInfo2 submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.commandBufferInfoCount = count;
	submitInfo.pCommandBufferInfos = cmdInfos;
	if (signalFence != nullptr)
	{
		submitInfo.signalSemaphoreInfoCount = 1;
		submitInfo.pSignalSemaphoreInfos = &signalInfo;
	}

	vkQueueSubmit2(m_Queue, 1, &submitInfo, VNULL);
}