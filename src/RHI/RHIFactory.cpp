/*****************************************************************//**
 * \file   RHIFactory.cpp
 * \brief  CreateRHIDeviceの実体
 * 
 * 作成者 keeeeep
 * 作成日 2026/10/13
 * 更新履歴 10.13 作成
 * *********************************************************************/
#include "IRHIDevice.hpp"
#include "D3D11/D3D11Device.hpp"
#include "D3D12/D3D12Device.hpp"
// Vulkan SDK が見つかったときだけ CMake が TE_RHI_VULKAN を定義する。SDK がなくても D3D だけでビルドできるようにするため
#if TE_RHI_VULKAN
#include "Vulkan/VulkanDevice.hpp"
#endif

std::unique_ptr<IRHIDevice> CreateRHIDevice(const RHIDeviceDesc& desc)
{
	switch (desc.backend)
	{
	case ERHIBackend::D3D11:
	{
		auto device = std::make_unique<D3D11Device>();
		if (!device->Init(desc)) return nullptr;
		return device;
	}
	case ERHIBackend::D3D12:
	{
		auto device = std::make_unique<D3D12Device>();
		if (!device->Init(desc)) return nullptr;
		return device;
	}
	case ERHIBackend::Vulkan:
	{
#if TE_RHI_VULKAN
		auto device = std::make_unique<VulkanDevice>();
		if (!device->Init(desc)) return nullptr;
		return device;
#else
		return nullptr;	// Vulkan なしでビルドしている
#endif
	}
	}
	return nullptr;
}