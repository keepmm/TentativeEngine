/*****************************************************************//**
 * \file   VulkanDescriptorTable.hpp
 * \brief  IRHIDescriptorTable の Vulkan 実装
 *
 * 作成者 keepm
 * 作成日 2026/10/23
 * 更新履歴 10.23 作成
 * *********************************************************************/
#pragma once

#include "VulkanTypes.hpp"
#include "../IRHIDescriptorTable.hpp"

class VulkanDevice;

class VulkanDescriptorTable final : public IRHIDescriptorTable
{
public:
	VulkanDescriptorTable(
		_In_ VulkanDevice& device,
		_In_ VkDescriptorPool pool,
		_In_ VkDescriptorSet set,
		_In_ uint32_t baseBinding,
		_In_ uint32_t count);
	~VulkanDescriptorTable() override;

	// ---- IRHIDescriptorTable ---- //
	uint32_t GetCount() const noexcept override { return m_Count; }
	void SetTexture(_In_ uint32_t index, _In_ const RHITextureRef& texture) override;
	void ClearTexture(_In_ uint32_t index) override;

	// ---- Vulkan バックエンド内部用 ---- //
	VkDescriptorSet GetVkDescriptorSet() const noexcept { return m_Set; }

private:
	VulkanDevice& m_Device;
	VkDescriptorPool			m_Pool = VNULL;
	VkDescriptorSet				m_Set = VNULL;
	uint32_t					m_BaseBinding = 0;
	uint32_t					m_Count = 0;
	std::vector<RHITextureRef>	m_Textures;	// 置いたテクスチャを生かしておく
};