// NOMINMAX は CMake で全体に定義している
#include "D3D11Device.hpp"
#include <algorithm>
#include <cstring>

static uint32_t GetBytesPerPixel(ERHIFormat format)
{
	switch (format)
	{
	case ERHIFormat::R8G8B8A8_UNORM:
	case ERHIFormat::R8G8B8A8_UNORM_SRGB:
	case ERHIFormat::B8G8R8A8_UNORM:
	case ERHIFormat::B8G8R8A8_UNORM_SRGB:
	case ERHIFormat::R32_FLOAT:
	case ERHIFormat::R32_UINT:
	case ERHIFormat::D32_FLOAT:
		return 4;
	case ERHIFormat::R16G16B16A16_FLOAT:
	case ERHIFormat::R32G32_FLOAT:
		return 8;
	case ERHIFormat::R32G32B32A32_FLOAT:
	case ERHIFormat::R32G32B32A32_UINT:
		return 16;
	case ERHIFormat::R16_UINT:
		return 2;
	default:
		return 4;
	}
}

bool D3D11Device::Init(const RHIDeviceDesc& desc)
{
	m_FramesInFlight = desc.frameInFlight;

	UINT flags = 0;
	if (desc.enableDebugLayer)
	{
		flags |= D3D11_CREATE_DEVICE_DEBUG;
	}

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};
	D3D_FEATURE_LEVEL chosenLevel;

	HRESULT hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		flags,
		featureLevels,
		_countof(featureLevels),
		D3D11_SDK_VERSION,
		m_Device.GetAddressOf(),
		&chosenLevel,
		m_Context.GetAddressOf());

	// デバッグレイヤー付きで失敗した場合は通常モードでリトライ
	if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG))
	{
		flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			flags,
			featureLevels,
			_countof(featureLevels),
			D3D11_SDK_VERSION,
			m_Device.GetAddressOf(),
			&chosenLevel,
			m_Context.GetAddressOf());
	}

	if (FAILED(hr)) return false;

	// DXGI Factory の取得
	ComPtr<IDXGIDevice> dxgiDevice;
	if (SUCCEEDED(m_Device.As(&dxgiDevice)))
	{
		ComPtr<IDXGIAdapter> adapter;
		if (SUCCEEDED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
		{
			adapter->GetParent(IID_PPV_ARGS(m_DxgiFactory.GetAddressOf()));
		}
	}

	return (m_Device && m_Context);
}

RHITextureRef D3D11Device::CreateTexture(const RHITextureDesc& desc)
{
	RHITextureDesc actualDesc = desc;
	if (actualDesc.mipLevels == 0)
	{
		actualDesc.mipLevels = 1;
	}
	if (actualDesc.depthOrArraySize == 0)
	{
		actualDesc.depthOrArraySize = 1;
	}
	if (actualDesc.sampleCount == 0)
	{
		actualDesc.sampleCount = 1;
	}

	D3D11_TEXTURE2D_DESC td{};
	td.Width = actualDesc.width;
	td.Height = actualDesc.height;
	td.MipLevels = actualDesc.mipLevels;
	td.ArraySize = (actualDesc.dimension == ERHITextureDimension::TextureCube) ? 6 : actualDesc.depthOrArraySize;
	td.Format = ToD3D11ResourceFormat(actualDesc.format, actualDesc.usage);
	td.SampleDesc.Count = actualDesc.sampleCount;
	td.SampleDesc.Quality = 0;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = ToD3D11TextureBindFlags(actualDesc.usage);
	td.CPUAccessFlags = 0;
	if (actualDesc.dimension == ERHITextureDimension::TextureCube)
	{
		td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
	}

	ComPtr<ID3D11Texture2D> tex;
	HRESULT hr = m_Device->CreateTexture2D(&td, nullptr, tex.GetAddressOf());
	if (FAILED(hr)) return nullptr;

	auto rhiTex = std::make_shared<D3D11Texture>(actualDesc, std::move(tex));
	if (!rhiTex->CreateViews(m_Device.Get())) return nullptr;
	return rhiTex;
}

RHIBufferRef D3D11Device::CreateBuffer(const RHIBufferDesc& desc, const void* initialData)
{
	D3D11_BUFFER_DESC bd{};
	bd.ByteWidth = static_cast<UINT>(desc.size);

	// 定数バッファは 256 バイトアライメントが必要
	if (HasAnyFlag(desc.usage, ERHIBufferUsage::ConstantBuffer))
	{
		bd.ByteWidth = (bd.ByteWidth + 255) & ~255;
	}

	bd.Usage = ToD3D11Usage(desc.memory);
	bd.BindFlags = ToD3D11BufferBindFlags(desc.usage);
	bd.CPUAccessFlags = ToD3D11CPUAccessFlags(desc.memory);

	D3D11_SUBRESOURCE_DATA sd{};
	if (initialData)
	{
		sd.pSysMem = initialData;
	}

	ComPtr<ID3D11Buffer> buf;
	HRESULT hr = m_Device->CreateBuffer(&bd, initialData ? &sd : nullptr, buf.GetAddressOf());
	if (FAILED(hr)) return nullptr;

	auto rhiBuf = std::make_shared<D3D11Buffer>(desc, std::move(buf));
	if (initialData && desc.memory == ERHIMemoryType::Upload)
	{
		rhiBuf->SetInitialData(initialData, desc.size);
	}
	return rhiBuf;
}

uint64_t D3D11Device::GetCopyableFootprints(
	IRHITexture* texture,
	uint32_t firstSubresource,
	uint32_t numSubresources,
	RHISubresourceFootprint* out)
{
	if (!texture || !out) return 0;
	const auto& td = texture->GetDesc();
	uint32_t bpp = GetBytesPerPixel(td.format);

	uint64_t total = 0;
	for (uint32_t i = 0; i < numSubresources; ++i)
	{
		uint32_t subIndex = firstSubresource + i;
		uint32_t mip = (td.mipLevels > 0) ? (subIndex % td.mipLevels) : 0;

		uint32_t w = (std::max)(1u, td.width >> mip);
		uint32_t h = (std::max)(1u, td.height >> mip);

		out[i].width = w;
		out[i].height = h;
		out[i].depth = 1;
		out[i].rowPitch = (w * bpp + 255) & ~255;
		out[i].rowSize = w * bpp;
		out[i].rowCount = h;
		out[i].offset = total;

		total += static_cast<uint64_t>(out[i].rowPitch) * h;
	}
	return total;
}

RHIPipelineLayoutRef D3D11Device::CreatePipelineLayout(const RHIPipelineLayoutDesc& desc)
{
	return std::make_shared<D3D11PipelineLayout>(m_Device.Get(), desc);
}

RHIPipelineStateRef D3D11Device::CreatePipelineState(const RHIGraphicsPipelineDesc& desc)
{
	if (!m_Device) return nullptr;

	ComPtr<ID3D11VertexShader> vs;
	if (desc.vs.data && desc.vs.size > 0)
	{
		if (FAILED(m_Device->CreateVertexShader(desc.vs.data, desc.vs.size, nullptr, vs.GetAddressOf())))
		{
			return nullptr;
		}
	}

	ComPtr<ID3D11PixelShader> ps;
	if (desc.ps.data && desc.ps.size > 0)
	{
		if (FAILED(m_Device->CreatePixelShader(desc.ps.data, desc.ps.size, nullptr, ps.GetAddressOf())))
		{
			return nullptr;
		}
	}

	ComPtr<ID3D11InputLayout> inputLayout;
	if (!desc.vertexAttributes.empty() && desc.vs.data)
	{
		std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
		elements.reserve(desc.vertexAttributes.size());

		for (const auto& attr : desc.vertexAttributes)
		{
			D3D11_INPUT_ELEMENT_DESC elem{};
			elem.SemanticName = attr.semanticName;
			elem.SemanticIndex = attr.semanticIndex;
			elem.Format = ToD3D11Format(attr.format);
			elem.InputSlot = attr.bufferSlot;
			elem.AlignedByteOffset = attr.offset;

			bool perInstance = (attr.bufferSlot < desc.vertexBuffers.size())
				? desc.vertexBuffers[attr.bufferSlot].perInstance
				: false;
			elem.InputSlotClass = perInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
			elem.InstanceDataStepRate = perInstance ? 1 : 0;
			elements.push_back(elem);
		}

		if (FAILED(m_Device->CreateInputLayout(
			elements.data(),
			static_cast<UINT>(elements.size()),
			desc.vs.data,
			desc.vs.size,
			inputLayout.GetAddressOf())))
		{
			return nullptr;
		}
	}

	// ラスタライザ
	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = ToD3D11FillMode(desc.rasterizer.fillMode);
	rd.CullMode = ToD3D11CullMode(desc.rasterizer.cullMode);
	rd.FrontCounterClockwise = desc.rasterizer.frontCounterClockwise ? TRUE : FALSE;
	rd.DepthBias = desc.rasterizer.depthBias;
	rd.DepthBiasClamp = desc.rasterizer.depthBiasClamp;
	rd.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
	rd.DepthClipEnable = desc.rasterizer.depthClipEnable ? TRUE : FALSE;
	rd.ScissorEnable = TRUE;

	ComPtr<ID3D11RasterizerState> rs;
	if (FAILED(m_Device->CreateRasterizerState(&rd, rs.GetAddressOf())))
	{
		return nullptr;
	}

	// 深度ステンシル
	D3D11_DEPTH_STENCIL_DESC dsd{};
	dsd.DepthEnable = (desc.dsvFormat != ERHIFormat::Unknown && (desc.depthStencil.depthTestEnable || desc.depthStencil.depthWriteEnable)) ? TRUE : FALSE;
	dsd.DepthWriteMask = desc.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	dsd.DepthFunc = desc.depthStencil.depthTestEnable ? ToD3D11Comparison(desc.depthStencil.depthFunc) : D3D11_COMPARISON_ALWAYS;
	dsd.StencilEnable = FALSE;

	ComPtr<ID3D11DepthStencilState> dss;
	if (FAILED(m_Device->CreateDepthStencilState(&dsd, dss.GetAddressOf())))
	{
		return nullptr;
	}

	// ブレンド
	D3D11_BLEND_DESC bd{};
	bd.RenderTarget[0].BlendEnable = FALSE;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	ComPtr<ID3D11BlendState> bs;
	if (FAILED(m_Device->CreateBlendState(&bd, bs.GetAddressOf())))
	{
		return nullptr;
	}

	return std::make_shared<D3D11PipelineState>(
		desc.layout,
		std::move(vs),
		std::move(ps),
		std::move(inputLayout),
		std::move(rs),
		std::move(dss),
		std::move(bs),
		desc.vertexBuffers);
}

RHIDescriptorTableRef D3D11Device::CreateDescriptorTable(IRHIPipelineLayout* layout, uint32_t slot)
{
	if (!layout || slot >= layout->GetSlotCount()) return nullptr;
	const auto& s = layout->GetSlot(slot);
	return std::make_shared<D3D11DescriptorTable>(s.shaderRegister, s.count);
}

RHICommandListRef D3D11Device::CreateCommandList(const char* debugName)
{
	(void)debugName;
	return std::make_shared<D3D11CommandList>(m_Context);
}

RHIFenceRef D3D11Device::CreateFence(uint64_t initialValue)
{
	return std::make_shared<D3D11Fence>(m_Device.Get(), initialValue);
}

RHISwapChainRef D3D11Device::CreateSwapChain(const RHISwapChainDesc& desc)
{
	if (!m_DxgiFactory || !m_Device) return nullptr;

	DXGI_SWAP_CHAIN_DESC1 sd{};
	sd.Width = desc.width;
	sd.Height = desc.height;
	sd.Format = ToD3D11Format(desc.format);
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = desc.bufferCount;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	ComPtr<IDXGISwapChain1> swapChain1;
	HRESULT hr = m_DxgiFactory->CreateSwapChainForHwnd(
		m_Device.Get(),
		static_cast<HWND>(desc.windowHandle),
		&sd,
		nullptr,
		nullptr,
		swapChain1.GetAddressOf());
	if (FAILED(hr)) return nullptr;

	auto sc = std::make_shared<D3D11SwapChain>(m_Device, swapChain1, desc);
	if (!sc->Init()) return nullptr;
	return sc;
}

void D3D11Device::Submit(
	IRHICommandList* const* lists,
	uint32_t count,
	IRHIFence* signalFence,
	uint64_t signalValue)
{
	(void)lists; (void)count;
	if (m_Context)
	{
		m_Context->Flush();
	}
	if (signalFence)
	{
		static_cast<D3D11Fence*>(signalFence)->Signal(signalValue, m_Context.Get());
	}
}

void D3D11Device::WaitIdle()
{
	if (m_Context)
	{
		m_Context->Flush();
	}
}

void D3D11Device::BeginFrame(uint32_t slot)
{
	(void)slot;
}
