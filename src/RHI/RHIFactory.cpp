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
//#include "Vulkan/VulkanDevice.hpp"

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
		// TODO: VulkanDevice 実装後に有効化
		// auto device = std::make_unique<VulkanDevice>();
		// if (!device->Init(desc)) return nullptr;
		// return device;
		return nullptr;
	}
	}
	return nullptr;
}