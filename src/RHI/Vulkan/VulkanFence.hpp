/*****************************************************************//**
 * \file   VulkanFence.hpp
 * \brief  IRHIFenceのVulkan実装(Timeline Semaphoreを使用)
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "VulkanTypes.hpp"
#include "../IRHIFence.hpp"

class VulkanFence final : public IRHIFence
{
public:
	VulkanFence(_In_ VkDevice device, _In_ VkSemaphore timelineSemaphore)
		: m_Device(device)
		, m_Semaphore(timelineSemaphore) {}

	~VulkanFence() override
	{
		if (m_Semaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(m_Device, m_Semaphore, nullptr);
			m_Semaphore = VK_NULL_HANDLE;
		}
	}

	uint64_t GetCompletedValue() const override
	{
		uint64_t value = 0;
		vkGetSemaphoreCounterValue(m_Device, m_Semaphore, &value);
		return value;
	}

	void Wait(_In_ uint64_t value)override
	{
		if (GetCompletedValue() >= value)return;

		VkSemaphoreWaitInfo waitInfo{};
		waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		waitInfo.semaphoreCount = 1;
		waitInfo.pSemaphores = &m_Semaphore;
		waitInfo.pValues = &value;

		// UINT64_MAX = 届くまで無制限に待つ
		vkWaitSemaphores(m_Device, &waitInfo, UINT64_MAX);
	}

	void* GetNativeFence() const noexcept override { return m_Semaphore; }
	VkSemaphore GetVkSemaphore() const noexcept { return m_Semaphore; }
private:
	VkDevice m_Device = VK_NULL_HANDLE;
	VkSemaphore m_Semaphore = VK_NULL_HANDLE;
};

