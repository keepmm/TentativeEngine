/*****************************************************************//**
 * \file   RHIFactory.cpp
 * \brief  CreateRHIDeviceの実体
 * 
 * 作成者 keeeeep
 * 作成日 2026/10/13
 * 更新履歴 10.13 作成
 * *********************************************************************/
#include "IRHIDevice.hpp"
#include "D3D12/D3D12Device.hpp"

std::unique_ptr<IRHIDevice> CreateRHIDevice(const RHIDeviceDesc& desc)
{
	switch (desc.backend)
	{
	case ERHIBackend::D3D12:
	{
		auto device = MakeUnique<D3D12Device>();
		if (!device->Init(desc)) return nullptr;
		return device;
	}
	case ERHIBackend::Vulkan:
		return nullptr;	// Phase 4
	}
	return nullptr;
}