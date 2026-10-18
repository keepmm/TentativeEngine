/*****************************************************************//**
 * \file   D3D12Pipeline.cpp
 * \brief  IRHIPipelineLayout / IRHIPipelineState の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#include "D3D12Pipeline.hpp"
#include "ThirdParty/d3dx12.h"
#include "Core/Log.hpp"

// ---------------------------------------------------//
//				D3D12PipelineLayout					  //
// ---------------------------------------------------//
std::unique_ptr<D3D12PipelineLayout> D3D12PipelineLayout::Create(
	ID3D12Device* device,
	const RHIPipelineLayoutDesc& desc)
{
	// テーブルの範囲はルートパラメータからポインタで指されるので、
	// シリアライズが終わるまで動かないよう先に容量を確保しておく
	std::vector<CD3DX12_DESCRIPTOR_RANGE> ranges;
	ranges.reserve(desc.slots.size());

	std::vector<CD3DX12_ROOT_PARAMETER> params(desc.slots.size());

	// ---- スロット → ルートパラメータ（添字がそのままルートパラメータ番号）---- //
	for (size_t i = 0; i < desc.slots.size(); ++i)
	{
		const RHIBindingSlot& s = desc.slots[i];
		const D3D12_SHADER_VISIBILITY visibility = ToD3D12Visibility(s.visibility);

		switch (s.type)
		{
		case ERHIBindingType::ConstantBuffer:		// b#: ルート CBV
			params[i].InitAsConstantBufferView(s.shaderRegister, s.registerSpace, visibility);
			break;

		case ERHIBindingType::StructuredBuffer:		// t#: ルート SRV
			params[i].InitAsShaderResourceView(s.shaderRegister, s.registerSpace, visibility);
			break;

		case ERHIBindingType::TextureTable:			// t# から count 枚: SRV テーブル
			ranges.emplace_back(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, s.count, s.shaderRegister, s.registerSpace);
			params[i].InitAsDescriptorTable(1, &ranges.back(), visibility);
			break;
		}
	}

	// ---- 静的サンプラー ---- //
	std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
	samplers.reserve(desc.staticSamplers.size());
	for (const auto& s : desc.staticSamplers)
	{
		D3D12_STATIC_SAMPLER_DESC d{};
		d.Filter			= ToD3D12Filter(s.filter, s.compareEnable);
		d.AddressU			= ToD3D12AddressMode(s.address);
		d.AddressV			= d.AddressU;
		d.AddressW			= d.AddressU;
		d.MipLODBias		= 0.0f;
		d.MaxAnisotropy		= s.maxAnisotropy;
		d.ComparisonFunc	= ToD3D12Comparison(s.compareFunc);
		d.BorderColor		= ToD3D12BorderColor(s.borderColor);
		d.MinLOD			= 0.0f;
		d.MaxLOD			= D3D12_FLOAT32_MAX;
		d.ShaderRegister	= s.shaderRegister;
		d.RegisterSpace		= s.registerSpace;
		d.ShaderVisibility	= ToD3D12Visibility(s.visibility);
		samplers.push_back(d);
	}

	// ---- シリアライズ → 生成 ---- //
	CD3DX12_ROOT_SIGNATURE_DESC rootDesc;
	rootDesc.Init(
		static_cast<UINT>(params.size()), params.data(),
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		blob.GetAddressOf(), error.GetAddressOf())))
	{
		LogError(std::string("[RHI] ルートシグネチャのシリアライズに失敗: ")
			+ (error ? static_cast<const char*>(error->GetBufferPointer()) : ""));
		return nullptr;
	}

	ComPtr<ID3D12RootSignature> rootSignature;
	if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf()))))
	{
		return nullptr;
	}
	SetD3D12DebugName(rootSignature.Get(), desc.debugName);

	return std::make_unique<D3D12PipelineLayout>(std::move(rootSignature), desc.slots);
}

// ---------------------------------------------------//
//				D3D12PipelineState					  //
// ---------------------------------------------------//
std::unique_ptr<D3D12PipelineState> D3D12PipelineState::Create(
	ID3D12Device* device,
	const RHIGraphicsPipelineDesc& desc)
{
	if (desc.layout == nullptr || !desc.vs.IsValid() ||
		desc.numRenderTargets > RHI_MAX_RENDER_TARGETS ||
		desc.vertexBuffers.size() > MAX_VERTEX_BUFFERS)
	{
		assert(!"CreatePipelineState: 未対応の RHIGraphicsPipelineDesc");
		return nullptr;
	}

	// ---- 頂点レイアウト ---- //
	std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
	elements.reserve(desc.vertexAttributes.size());
	for (const RHIVertexAttribute& a : desc.vertexAttributes)
	{
		if (a.bufferSlot >= desc.vertexBuffers.size())
		{
			assert(!"CreatePipelineState: vertexBuffers に無いスロットを参照している");
			return nullptr;
		}
		const bool perInstance = desc.vertexBuffers[a.bufferSlot].perInstance;

		D3D12_INPUT_ELEMENT_DESC e{};
		e.SemanticName			= a.semanticName;
		e.SemanticIndex			= a.semanticIndex;
		e.Format				= ToDXGIFormat(a.format);
		e.InputSlot				= a.bufferSlot;
		e.AlignedByteOffset		= a.offset;
		e.InputSlotClass		= perInstance
								? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
								: D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		e.InstanceDataStepRate	= perInstance ? 1 : 0;
		elements.push_back(e);
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
	pd.pRootSignature	= static_cast<D3D12PipelineLayout*>(desc.layout.get())->GetRootSignature();
	pd.VS				= { desc.vs.data, desc.vs.size };
	pd.PS				= { desc.ps.data, desc.ps.size };	// 深度だけのパスは PS なしでよい
	pd.InputLayout		= { elements.data(), static_cast<UINT>(elements.size()) };

	// 使わない項目も正しい値で埋まっている既定値から始めて、RHI の設定で上書きする
	pd.RasterizerState		= CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pd.BlendState			= CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pd.DepthStencilState	= CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	// ---- ラスタライザ ---- //
	D3D12_RASTERIZER_DESC& rs = pd.RasterizerState;
	rs.FillMode					= ToD3D12FillMode(desc.rasterizer.fillMode);
	rs.CullMode					= ToD3D12CullMode(desc.rasterizer.cullMode);
	rs.FrontCounterClockwise	= desc.rasterizer.frontCounterClockwise ? TRUE : FALSE;
	rs.DepthBias				= desc.rasterizer.depthBias;
	rs.DepthBiasClamp			= desc.rasterizer.depthBiasClamp;
	rs.SlopeScaledDepthBias		= desc.rasterizer.slopeScaledDepthBias;
	rs.DepthClipEnable			= desc.rasterizer.depthClipEnable ? TRUE : FALSE;

	// ---- 深度 ---- //
	// D3D12 は DepthEnable=FALSE だと「テストも書き込みも」止まる。
	// 「テストせずに書くだけ」は ALWAYS で表す
	const bool hasDepth = desc.dsvFormat != ERHIFormat::Unknown;
	const RHIDepthStencilDesc& depth = desc.depthStencil;
	D3D12_DEPTH_STENCIL_DESC& ds = pd.DepthStencilState;
	ds.DepthEnable		= (hasDepth && (depth.depthTestEnable || depth.depthWriteEnable)) ? TRUE : FALSE;
	ds.DepthWriteMask	= depth.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	ds.DepthFunc		= depth.depthTestEnable ? ToD3D12Comparison(depth.depthFunc) : D3D12_COMPARISON_FUNC_ALWAYS;

	// ---- ブレンド（RT ごと）---- //
	pd.BlendState.IndependentBlendEnable = TRUE;
	for (uint32_t i = 0; i < desc.numRenderTargets; ++i)
	{
		const RHIBlendDesc& b = desc.blend[i];
		D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[i];
		rt.BlendEnable				= b.blendEnable ? TRUE : FALSE;
		rt.SrcBlend					= ToD3D12Blend(b.srcColor);
		rt.DestBlend				= ToD3D12Blend(b.dstColor);
		rt.BlendOp					= ToD3D12BlendOp(b.colorOp);
		rt.SrcBlendAlpha			= ToD3D12Blend(b.srcAlpha);
		rt.DestBlendAlpha			= ToD3D12Blend(b.dstAlpha);
		rt.BlendOpAlpha				= ToD3D12BlendOp(b.alphaOp);
		rt.RenderTargetWriteMask	= ToD3D12WriteMask(b.writeMask);
	}

	// ---- 出力先 / トポロジー ---- //
	pd.SampleMask				= UINT_MAX;
	pd.PrimitiveTopologyType	= ToD3D12TopologyType(desc.topology);
	pd.NumRenderTargets			= desc.numRenderTargets;
	for (uint32_t i = 0; i < desc.numRenderTargets; ++i)
	{
		pd.RTVFormats[i] = ToDXGIFormat(desc.rtvFormat[i]);
	}
	pd.DSVFormat				= hasDepth ? ToDXGIFormat(desc.dsvFormat) : DXGI_FORMAT_UNKNOWN;
	pd.SampleDesc.Count			= desc.sampleCount;

	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(pipeline.GetAddressOf()))))
	{
		// ルートシグネチャとシェーダーの食い違いなど、原因はデバッグレイヤーの出力に出る
		LogError(std::string("[RHI] PSO の生成に失敗: ")
			+ (desc.debugName != nullptr ? desc.debugName : "(名前なし)"));
		return nullptr;
	}
	SetD3D12DebugName(pipeline.Get(), desc.debugName);

	auto pso = std::make_unique<D3D12PipelineState>();
	pso->m_Pipeline				= std::move(pipeline);
	pso->m_Layout				= desc.layout;
	pso->m_Topology				= ToD3D12Topology(desc.topology);
	pso->m_VertexBufferCount	= static_cast<uint32_t>(desc.vertexBuffers.size());
	for (uint32_t i = 0; i < pso->m_VertexBufferCount; ++i)
	{
		pso->m_VertexStrides[i] = desc.vertexBuffers[i].stride;
	}
	return pso;
}
