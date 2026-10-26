/*****************************************************************//**
 * \file   VulkanDescriptorTable.cpp
 * \brief  IRHIDescriptorTable の Vulkan 実装
 *
 * 作成者 keepm
 * 作成日 2026/10/23
 * 更新履歴 10.23 作成
 * *********************************************************************/
#include "VulkanDescriptorTable.hpp"
#include "VulkanDevice.hpp"
#include "VulkanResource.hpp"

#include <cassert>

VulkanDescriptorTable::VulkanDescriptorTable(
	VulkanDevice& device,
	VkDescriptorPool pool,
	VkDescriptorSet set,
	uint32_t baseBinding,
	uint32_t count)
	: m_Device(device)
	, m_Pool(pool)
	, m_Set(set)
	, m_BaseBinding(baseBinding)
	, m_Count(count)
	, m_Textures(count)
{}

VulkanDescriptorTable::~VulkanDescriptorTable()
{
	if (m_Set != VNULL && m_Pool != VNULL)
	{
		vkFreeDescriptorSets(m_Device.GetVkDevice(), m_Pool, 1, &m_Set);
		m_Set = VNULL;
	}
}

void VulkanDescriptorTable::SetTexture(uint32_t index, const RHITextureRef& texture)
{
	assert(index < m_Count);

	auto* vkTex = static_cast<VulkanTexture*>(texture.get());
	if (vkTex == nullptr || vkTex->GetVkImageView() == VNULL)
	{
		assert(!"SetTexture: ShaderResource 用途で作っていないテクスチャ");
		ClearTexture(index);
		return;
	}

	VkDescriptorImageInfo imageInfo{};
	imageInfo.sampler = VNULL;
	imageInfo.imageView = vkTex->GetVkImageView();
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_Set;
	write.dstBinding = m_BaseBinding + index;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(m_Device.GetVkDevice(), 1, &write, 0, nullptr);
	m_Textures[index] = texture;
}

void VulkanDescriptorTable::ClearTexture(uint32_t index)
{
	assert(index < m_Count);
	m_Textures[index].reset();
}