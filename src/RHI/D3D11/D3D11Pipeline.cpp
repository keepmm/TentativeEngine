/*****************************************************************//**
 * \file   D3D11Pipeline.cpp
 * \brief  IRHIPipelineLayout / IRHIPipelineState の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#include "D3D11Pipeline.hpp"

// ------------------------------------------------------------------ //
//                        D3D11PipelineLayout                         //
// ------------------------------------------------------------------ //

D3D11PipelineLayout::D3D11PipelineLayout(ID3D11Device* device, const RHIPipelineLayoutDesc& desc)
	: m_Desc(desc)
{
	m_Desc.debugName = nullptr;

	// 静的サンプラーの作成
	for (const auto& s : desc.staticSamplers)
	{
		D3D11_SAMPLER_DESC sd{};
		sd.Filter = ToD3D11Filter(s.filter, s.compareEnable);
		sd.AddressU = ToD3D11AddressMode(s.address);
		sd.AddressV = ToD3D11AddressMode(s.address);
		sd.AddressW = ToD3D11AddressMode(s.address);
		sd.ComparisonFunc = ToD3D11Comparison(s.compareFunc);
		sd.MaxAnisotropy = s.maxAnisotropy;
		sd.MinLOD = 0.0f;
		sd.MaxLOD = D3D11_FLOAT32_MAX;

		ComPtr<ID3D11SamplerState> sampler;
		if (SUCCEEDED(device->CreateSamplerState(&sd, sampler.GetAddressOf())))
		{
			m_Samplers.push_back({ s.shaderRegister, std::move(sampler) });
		}
	}
}

void D3D11PipelineLayout::BindStaticSamplers(ID3D11DeviceContext* context) const
{
	for (const auto& b : m_Samplers)
	{
		ID3D11SamplerState* s = b.sampler.Get();
		context->PSSetSamplers(b.shaderRegister, 1, &s);
		context->VSSetSamplers(b.shaderRegister, 1, &s);
	}
}

// ------------------------------------------------------------------ //
//                         D3D11PipelineState                         //
// ------------------------------------------------------------------ //

D3D11PipelineState::D3D11PipelineState(
	RHIPipelineLayoutRef layout,
	ComPtr<ID3D11VertexShader> vs,
	ComPtr<ID3D11PixelShader> ps,
	ComPtr<ID3D11InputLayout> inputLayout,
	ComPtr<ID3D11RasterizerState> rasterizer,
	ComPtr<ID3D11DepthStencilState> depthStencil,
	ComPtr<ID3D11BlendState> blend,
	const std::vector<RHIVertexBufferLayout>& vertexBuffers)
	: m_Layout(std::move(layout))
	, m_VS(std::move(vs))
	, m_PS(std::move(ps))
	, m_InputLayout(std::move(inputLayout))
	, m_RasterizerState(std::move(rasterizer))
	, m_DepthStencilState(std::move(depthStencil))
	, m_BlendState(std::move(blend))
	, m_VertexBuffers(vertexBuffers)
{
}

void D3D11PipelineState::Apply(ID3D11DeviceContext* context) const
{
	// シェーダーと入力レイアウトのバインド
	context->VSSetShader(m_VS.Get(), nullptr, 0);
	context->PSSetShader(m_PS.Get(), nullptr, 0);
	context->IASetInputLayout(m_InputLayout.Get());
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ステート群の適用
	context->RSSetState(m_RasterizerState.Get());
	context->OMSetDepthStencilState(m_DepthStencilState.Get(), 0);
	context->OMSetBlendState(m_BlendState.Get(), nullptr, 0xFFFFFFFF);

	// レイアウトに属する静的サンプラーを自動適用
	if (m_Layout)
	{
		static_cast<D3D11PipelineLayout*>(m_Layout.get())->BindStaticSamplers(context);
	}
}
