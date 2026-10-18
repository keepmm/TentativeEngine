/*****************************************************************//**
 * \file   D3D11Pipeline.hpp
 * \brief  IRHIPipelineLayout / IRHIPipelineState の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include "D3D11Types.hpp"
#include "../IRHIPipeline.hpp"

class D3D11PipelineLayout final : public IRHIPipelineLayout
{
public:
	D3D11PipelineLayout(_In_ ID3D11Device* device, _In_ const RHIPipelineLayoutDesc& desc);
	~D3D11PipelineLayout() override = default;

	uint32_t GetSlotCount() const noexcept override { return static_cast<uint32_t>(m_Desc.slots.size()); }
	ERHIBindingType GetSlotType(_In_ uint32_t slot) const noexcept override { return m_Desc.slots[slot].type; }
	const RHIBindingSlot& GetSlot(_In_ uint32_t slot) const noexcept override { return m_Desc.slots[slot]; }
	void* GetNativeLayout() const noexcept override { return nullptr; }

	// 静的サンプラーの適用
	void BindStaticSamplers(_In_ ID3D11DeviceContext* context) const;

private:
	RHIPipelineLayoutDesc m_Desc;
	struct StaticSamplerBinding
	{
		uint32_t shaderRegister = 0;
		ComPtr<ID3D11SamplerState> sampler;
	};
	std::vector<StaticSamplerBinding> m_Samplers;
};

class D3D11PipelineState final : public IRHIPipelineState
{
public:
	D3D11PipelineState(
		_In_ RHIPipelineLayoutRef layout,
		_In_ ComPtr<ID3D11VertexShader> vs,
		_In_ ComPtr<ID3D11PixelShader> ps,
		_In_ ComPtr<ID3D11InputLayout> inputLayout,
		_In_ ComPtr<ID3D11RasterizerState> rasterizer,
		_In_ ComPtr<ID3D11DepthStencilState> depthStencil,
		_In_ ComPtr<ID3D11BlendState> blend,
		_In_ const std::vector<RHIVertexBufferLayout>& vertexBuffers);
	~D3D11PipelineState() override = default;

	IRHIPipelineLayout* GetLayout() const noexcept override { return m_Layout.get(); }
	void* GetNativePipeline() const noexcept override { return nullptr; }

	/// @brief パイプラインをイミディエイトコンテキストへ一括適用する
	void Apply(_In_ ID3D11DeviceContext* context) const;

	uint32_t GetVertexBufferStride(_In_ uint32_t slot) const noexcept
	{
		return (slot < m_VertexBuffers.size()) ? m_VertexBuffers[slot].stride : 0;
	}

private:
	RHIPipelineLayoutRef m_Layout;
	ComPtr<ID3D11VertexShader> m_VS;
	ComPtr<ID3D11PixelShader> m_PS;
	ComPtr<ID3D11InputLayout> m_InputLayout;
	ComPtr<ID3D11RasterizerState> m_RasterizerState;
	ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
	ComPtr<ID3D11BlendState> m_BlendState;
	std::vector<RHIVertexBufferLayout> m_VertexBuffers;
};
