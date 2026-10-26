/*****************************************************************//**
 * \file   VulkanPipeline.hpp
 * \brief  IRHIPipelineLayout / IRHIPipelineState の Vulkan 実装
 *
 * 作成者 keepm
 * 作成日 2026/10/23
 * 更新履歴 10.23 作成
 * *********************************************************************/
#pragma once

#include "VulkanTypes.hpp"
#include "../IRHIPipeline.hpp"

class VulkanPipelineLayout final : public IRHIPipelineLayout
{
public:
	static std::unique_ptr<VulkanPipelineLayout> Create(
		_In_ VkDevice device,
		_In_ const RHIPipelineLayoutDesc& desc);

	VulkanPipelineLayout(
		_In_ VkDevice device,
		_In_ VkPipelineLayout pipelineLayout,
		_In_ std::vector<VkDescriptorSetLayout> setLayouts,
		_In_ std::vector<VkSampler> immutableSamplers,
		_In_ std::vector<RHIBindingSlot> slots);
	~VulkanPipelineLayout() override;

	uint32_t GetSlotCount() const noexcept override { return static_cast<uint32_t>(m_Slots.size()); }
	ERHIBindingType GetSlotType(_In_ uint32_t slot) const noexcept override { return m_Slots[slot].type; }
	const RHIBindingSlot& GetSlot(_In_ uint32_t slot) const noexcept override { return m_Slots[slot]; }
	void* GetNativeLayout() const noexcept override { return m_PipelineLayout; }

	// ---- Vulkan バックエンド内部用 ---- //
	VkPipelineLayout GetVkPipelineLayout() const noexcept { return m_PipelineLayout; }
	VkDescriptorSetLayout GetVkSetLayout(_In_ uint32_t slot) const noexcept { return m_SetLayouts[slot]; }

private:
	VkDevice							m_Device = VNULL;
	VkPipelineLayout					m_PipelineLayout = VNULL;
	std::vector<VkDescriptorSetLayout>	m_SetLayouts;
	std::vector<VkSampler>				m_ImmutableSamplers;
	std::vector<RHIBindingSlot>			m_Slots;
};

class VulkanPipelineState final : public IRHIPipelineState
{
public:
	static constexpr uint32_t MAX_VERTEX_BUFFERS = 8;

	static std::unique_ptr<VulkanPipelineState> Create(
		_In_ VkDevice device,
		_In_ const RHIGraphicsPipelineDesc& desc);

	VulkanPipelineState(
		_In_ VkDevice device,
		_In_ VkPipeline pipeline,
		_In_ RHIPipelineLayoutRef layout);
	~VulkanPipelineState() override;

	IRHIPipelineLayout* GetLayout() const noexcept override { return m_Layout.get(); }
	void* GetNativePipeline() const noexcept override { return m_Pipeline; }

	// ---- Vulkan バックエンド内部用 ---- //
	VkPipeline GetVkPipeline() const noexcept { return m_Pipeline; }
	VkPipelineLayout GetVkPipelineLayout() const noexcept
	{
		return static_cast<VulkanPipelineLayout*>(m_Layout.get())->GetVkPipelineLayout();
	}

private:
	VkDevice				m_Device = VNULL;
	VkPipeline				m_Pipeline = VNULL;
	RHIPipelineLayoutRef	m_Layout;
};