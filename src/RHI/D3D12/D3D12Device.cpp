/*****************************************************************//**
 * \file   D3D12Device.cpp
 * \brief  IRHIDevice の実装
 * 
 * 作成者 keeeeeeeep
 * 作成日 2026/10/13
 * 更新履歴 10.13 作成
 * *********************************************************************/
#include "D3D12Device.hpp"
#include "D3D12Resource.hpp"
#include "D3D12CommandList.hpp"
#include "D3D12DescriptorTable.hpp"
#include "D3D12Fence.hpp"
#include "D3D12Pipeline.hpp"
#include "D3D12SwapChain.hpp"
#include "ThirdParty/d3dx12.h"
#include "Core/Log.hpp"

#include <cstring>

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

namespace
{
	constexpr UINT VENDOR_NVIDIA = 0x10DE;
	constexpr D3D_FEATURE_LEVEL MIN_FEATURE_LEVEL = D3D_FEATURE_LEVEL_11_0;

	std::string toUtf8(const wchar_t* w)
	{
		const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
		if (len <= 1)return {};
		std::string s(static_cast<size_t>(len - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
		return s;
	}

		bool IsUsableAdapter(IDXGIAdapter1 * adapter, DXGI_ADAPTER_DESC1& outDesc)
		{
			if (FAILED(adapter->GetDesc1(&outDesc)))return false;
			if (outDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)return false;

			// 最後の引数がnullptr = 作れるかどうかだけ調べる
			return SUCCEEDED(D3D12CreateDevice(adapter, MIN_FEATURE_LEVEL, __uuidof(ID3D12Device), nullptr));
		}
}

// ---- 生成 / 破棄 ---- //
D3D12Device::~D3D12Device()
{
	if (m_Queue && m_IdleFence)
	{
		WaitIdle();
	}
	FlushAllPending();
	assert(m_LiveObjects.load() == 0 && "RHI オブジェクトがデバイスより長生きしている（先に全部手放すこと）");
}

bool D3D12Device::Init(const RHIDeviceDesc& desc)
{
	assert(desc.frameInFlight > 0 && "RHIDeviceDesc::frameInFlight に RTV_NUM を渡すこと");
	if (desc.frameInFlight == 0)return false;

	m_FrameInFlight = desc.frameInFlight;
	m_PendingDelete.resize(m_FrameInFlight);

	// デバッグレイヤーの作成
	UINT factoryFlags = 0;
	if (desc.enableDebugLayer && EnableDebugLayer(desc.enableGpuValidation))
	{
		m_DebugLayerEnabled = true;
		factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}

	// Factoryの作成
	if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_Factory.ReleaseAndGetAddressOf()))))
	{
		return false;
	}

	// アダプタ選択
	if (!SelectAdapter())
	{
		LogError("[RHI] D3D12 が使える GPU が見つかりません");
		return false;
	}

	// デバイスの作成
	if (FAILED(D3D12CreateDevice(m_Adapter.Get(), MIN_FEATURE_LEVEL, IID_PPV_ARGS(m_Device.ReleaseAndGetAddressOf()))))
	{
		return false;
	}
	if (m_DebugLayerEnabled)
	{
		SetupInfoQueue();
	}

	// グラフィックキューとWaitIdleフェンス
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_Queue.ReleaseAndGetAddressOf()))) || 
		FAILED(m_Device->CreateFence(0,D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(m_IdleFence.ReleaseAndGetAddressOf()))))
	{
		return false;
	}

	// CPU専用デスクリプタヒープ + デスクリプタテーブル用のシェーダー可視ヒープ
	if (!m_SrvHeap.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_HEAP_CAPACITY) ||
		!m_RtvHeap.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_HEAP_CAPACITY) ||
		!m_DsvHeap.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_HEAP_CAPACITY) ||
		!m_ShaderVisibleHeap.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			SHADER_VISIBLE_HEAP_CAPACITY, true))
	{
		return false;
	}
	SetD3D12DebugName(m_ShaderVisibleHeap.GetHeap(), "RHI Shader Visible Heap");

	// テーブルの空き枠に置く null SRV（読むと 0 が返る）
	m_NullSrvIndex = m_SrvHeap.Allocate();
	D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc{};
	nullDesc.Format						= DXGI_FORMAT_R8G8B8A8_UNORM;
	nullDesc.ViewDimension				= D3D12_SRV_DIMENSION_TEXTURE2D;
	nullDesc.Shader4ComponentMapping	= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullDesc.Texture2D.MipLevels		= 1;
	m_Device->CreateShaderResourceView(nullptr, &nullDesc, m_SrvHeap.Cpu(m_NullSrvIndex));

	// 機能情報
	QueryCaps();
	return true;
}

bool D3D12Device::EnableDebugLayer(bool gpuValidation)
{
	char noDebug[8] = {};
	if (GetEnvironmentVariableA("DX12_NO_DEBUG_LAYER", noDebug, sizeof(noDebug)) != 0 && noDebug[0] == '1')
	{
		return false;
	}

	ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
	{
		dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	}

	ComPtr<ID3D12Debug> debug;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))return false;
	debug->EnableDebugLayer();

	if (gpuValidation)
	{
		// GPUベースの検証
		ComPtr<ID3D12Debug1> debug1;
		if (SUCCEEDED(debug.As(&debug1)))
		{
			debug1->SetEnableGPUBasedValidation(TRUE);
		}
	}
	return true;
}

bool D3D12Device::SelectAdapter()
{
	struct Candidate
	{
		ComPtr<IDXGIAdapter1> adapter;
		UINT vendorId;
	};
	std::vector<Candidate> candidates;

	// ---- 列挙 ---- //
	ComPtr<IDXGIFactory6>factory6;
	ComPtr<IDXGIAdapter1>adapter;

	if (SUCCEEDED(m_Factory.As(&factory6)))
	{
		// DXGI 1.6 : ノートPCでもディスクリート GPU が先頭に来る
		for (UINT i = 0;
			SUCCEEDED(factory6->EnumAdapterByGpuPreference(
				i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())));
				++i)
		{
			DXGI_ADAPTER_DESC1 desc{};
			if (IsUsableAdapter(adapter.Get(), desc))
			{
				candidates.push_back({ adapter,desc.VendorId });
			}
		}
	}
	else
	{
		// DXGI 1.6がない古い環境
		for (UINT i = 0; SUCCEEDED(m_Factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf())); ++i)
		{
			DXGI_ADAPTER_DESC1 desc{};
			if (IsUsableAdapter(adapter.Get(), desc))
			{
				candidates.push_back({ adapter,desc.VendorId });
			}
		}
	}

	if (candidates.empty()) return false;

	// ---- NVIDIAを最優先に選択
	for (const Candidate& c : candidates)
	{
		if (c.vendorId == VENDOR_NVIDIA)
		{
			m_Adapter = c.adapter;
			return true;
		}
	}

	m_Adapter = candidates.front().adapter;
	return true;
}

void D3D12Device::SetupInfoQueue()
{
	ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(m_Device.As(&infoQueue)))
	{
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
	}
}

void D3D12Device::QueryCaps()
{
	DXGI_ADAPTER_DESC1 desc{};
	m_Adapter->GetDesc1(&desc);
	m_Caps.adapterName = toUtf8(desc.Description);

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
	if (SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
	{
		m_Caps.meshShader = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	}

	m_Caps.constantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
	m_Caps.maxTextureSize2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;

	LogInfo("[RHI] D3D12 アダプタ: " + m_Caps.adapterName + " (VRAM " + std::to_string(desc.DedicatedVideoMemory / (1024 * 1024)) + " MB)");
}

void D3D12Device::DeferDelete(IRHIObject* obj)
{
	std::lock_guard<std::mutex> lock(m_PendingMutex);
	m_PendingDelete[m_CurrentSlot].emplace_back(obj);
}

void D3D12Device::FlushAllPending()
{
	// 壊したオブジェクトが他のオブジェクトを手放すことがあるので。空になるまで繰り返す
	for (;;)
	{
		std::vector<std::unique_ptr<IRHIObject>> dead;
		{
			std::lock_guard<std::mutex> lock(m_PendingMutex);
			for (auto& list : m_PendingDelete)
			{
				for (auto& obj : list) dead.push_back(std::move(obj));
				list.clear();
			}
		}
		if (dead.empty()) break;

		m_LiveObjects -= static_cast<int64_t>(dead.size());
		dead.clear();
	}
}

RHITextureRef D3D12Device::CreateTexture(const RHITextureDesc& desc)
{
	const bool wantsRT = HasAnyFlag(desc.usage, ERHITextureUsage::RenderTarget);
	const bool wantsDS = HasAnyFlag(desc.usage, ERHITextureUsage::DepthStencil);

	// 今のエンジンで使わない組み合わせははじく
	if (desc.sampleCount != 1 ||
		desc.format == ERHIFormat::Unknown ||
		wantsDS != IsDepthFormat(desc.format) ||
		(desc.dimension == ERHITextureDimension::TextureCube && desc.depthOrArraySize % 6 != 0) ||
		(desc.dimension == ERHITextureDimension::Texture3D && (wantsRT || wantsDS)))
	{
		assert(!"CreateTexture: 未対応の RHITextureDesc");
		return nullptr;
	}

	// ---- リソース記述 ---- //
	D3D12_RESOURCE_DESC rd{};
	rd.Dimension = (desc.dimension == ERHITextureDimension::Texture3D)
		? D3D12_RESOURCE_DIMENSION_TEXTURE3D
		: D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rd.Width = desc.width;
	rd.Height = desc.height;
	rd.DepthOrArraySize = static_cast<UINT16>(desc.depthOrArraySize);
	rd.MipLevels = static_cast<UINT16>(desc.mipLevels);
	rd.Format = ToDXGIResourceFormat(desc.format, desc.usage);
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rd.Flags = ToD3D12ResourceFlags(desc.usage);

	// ---- 最適化クリア値 ---- //
	D3D12_CLEAR_VALUE clear{};
	const D3D12_CLEAR_VALUE* pClear = nullptr;
	if (wantsRT || wantsDS)
	{
		clear.Format = ToDXGIFormat(desc.format);
		if (wantsDS)
		{
			clear.DepthStencil.Depth = desc.clearValue.depth;
			clear.DepthStencil.Stencil = desc.clearValue.stencil;
		}
		else
		{
			for (int i = 0; i < 4; ++i)
			{
				clear.Color[i] = desc.clearValue.color[i];
			}
		}
		pClear = &clear;
	}

	// ---- 生成 ---- //
	const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> resource;
	if (FAILED(m_Device->CreateCommittedResource(
		&heap, D3D12_HEAP_FLAG_NONE, &rd, ToD3D12ResourceStates(ERHIResourceState::Undefined),
		pClear, IID_PPV_ARGS(resource.GetAddressOf()))))
	{
		return nullptr;
	}
	SetD3D12DebugName(resource.Get(), desc.debugName);

	auto texture = std::make_unique<D3D12Texture>(*this, desc, std::move(resource));
	if (!texture->CreateViews())
	{
		LogError("[RHI] デスクリプタヒープが満杯です");
		return nullptr;
	}
	return MakeRef(std::move(texture));
}

RHIBufferRef D3D12Device::CreateBuffer(const RHIBufferDesc& desc, const void* initialData)
{
	if (desc.size == 0 ||
		// 初期データを直接かけるのはUploadだけ
		initialData != nullptr && desc.memory != ERHIMemoryType::Upload ||
		(HasAnyFlag(desc.usage, ERHIBufferUsage::UnorderedAccess) && desc.memory != ERHIMemoryType::GpuOnly))
	{
		assert(!"CreateBuffer: 未対応の RHIBufferDesc");
		return nullptr;
	}

	RHIBufferDesc actual = desc;
	if (HasAnyFlag(desc.usage, ERHIBufferUsage::ConstantBuffer))
	{
		const uint64_t a = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		actual.size = (desc.size + a - 1) & ~(a - 1);
	}

	const CD3DX12_HEAP_PROPERTIES heap(ToD3D12HeapType(desc.memory));
	const CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(actual.size, ToD3D12ResourceFlags(desc.usage));

	ComPtr<ID3D12Resource> resource;
	if (FAILED(m_Device->CreateCommittedResource(
		&heap, D3D12_HEAP_FLAG_NONE, &rd,
		GetInitialBufferState(desc.memory),	// Upload=GENERIC_READ / ReadBack=COPY_DEST で固定
		nullptr, IID_PPV_ARGS(resource.GetAddressOf()))))
	{
		return nullptr;
	}
	SetD3D12DebugName(resource.Get(), desc.debugName);

	auto buffer = std::make_unique<D3D12Buffer>(actual, std::move(resource));
	if (initialData != nullptr)
	{
		void* dst = buffer->Map();
		if (dst == nullptr) return nullptr;
		std::memcpy(dst, initialData, static_cast<size_t>(desc.size));
	}
	return MakeRef(std::move(buffer));
}

uint64_t D3D12Device::GetCopyableFootprints(
	IRHITexture* texture,
	uint32_t firstSubresource,
	uint32_t numSubresources,
	RHISubresourceFootprint* out)
{
	if (texture == nullptr || numSubresources == 0 || out == nullptr) return 0;

	// 実際のリソースの記述（深度 + SRV なら TYPELESS になっている）から求める
	const D3D12_RESOURCE_DESC desc = static_cast<D3D12Texture*>(texture)->GetResource()->GetDesc();

	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
	std::vector<UINT>	rowCounts(numSubresources);
	std::vector<UINT64>	rowSizes(numSubresources);
	UINT64 totalBytes = 0;
	m_Device->GetCopyableFootprints(&desc, firstSubresource, numSubresources, 0,
		layouts.data(), rowCounts.data(), rowSizes.data(), &totalBytes);
	if (totalBytes == UINT64_MAX) return 0;	// サブリソースの範囲がおかしい

	for (uint32_t i = 0; i < numSubresources; ++i)
	{
		out[i].offset	= layouts[i].Offset;
		out[i].rowPitch	= layouts[i].Footprint.RowPitch;
		out[i].rowSize	= static_cast<uint32_t>(rowSizes[i]);
		out[i].rowCount	= rowCounts[i];
		out[i].width	= layouts[i].Footprint.Width;
		out[i].height	= layouts[i].Footprint.Height;
		out[i].depth	= layouts[i].Footprint.Depth;
	}
	return totalBytes;
}

void D3D12Device::WaitIdle()
{
	const uint64_t value = ++m_IdleFenceValue;
	m_Queue->Signal(m_IdleFence.Get(), value);
	// イベントに nullptr エお渡すと値に届くまで子の呼び出しがブロックされる
	m_IdleFence->SetEventOnCompletion(value, nullptr);
}

void D3D12Device::BeginFrame(uint32_t slot)
{
	assert(slot < m_FrameInFlight);

	std::vector<std::unique_ptr<IRHIObject>> dead;
	{
		std::lock_guard<std::mutex> lock(m_PendingMutex);
		m_CurrentSlot = slot;
		dead.swap(m_PendingDelete[slot]);
	}

	// ロックの外で壊す
	m_LiveObjects -= static_cast<int64_t>(dead.size());
	dead.clear();
}

// ---------------------------------------------------//
//			パイプライン / スワップチェイン				  //
// ---------------------------------------------------//
RHIPipelineLayoutRef D3D12Device::CreatePipelineLayout(const RHIPipelineLayoutDesc& desc)
{
	auto layout = D3D12PipelineLayout::Create(m_Device.Get(), desc);
	if (!layout) return nullptr;
	return MakeRef(std::move(layout));
}

RHIPipelineStateRef D3D12Device::CreatePipelineState(const RHIGraphicsPipelineDesc& desc)
{
	auto pipeline = D3D12PipelineState::Create(m_Device.Get(), desc);
	if (!pipeline) return nullptr;
	return MakeRef(std::move(pipeline));
}

RHIDescriptorTableRef D3D12Device::CreateDescriptorTable(IRHIPipelineLayout* layout, uint32_t slot)
{
	if (layout == nullptr || slot >= layout->GetSlotCount() ||
		layout->GetSlotType(slot) != ERHIBindingType::TextureTable)
	{
		assert(!"CreateDescriptorTable: TextureTable ではないスロット");
		return nullptr;
	}

	// D3D12 は枚数だけ分かればよい（Vulkan はここでレイアウトから VkDescriptorSet を割り当てる）
	const uint32_t count = layout->GetSlot(slot).count;
	const uint32_t start = m_ShaderVisibleHeap.AllocateRange(count);
	if (start == D3D12DescriptorHeap::INVALID_INDEX)
	{
		LogError("[RHI] シェーダー可視ヒープが満杯です");
		return nullptr;
	}
	return MakeRef(std::make_unique<D3D12DescriptorTable>(*this, start, count));
}

RHISwapChainRef D3D12Device::CreateSwapChain(const RHISwapChainDesc& desc)
{
	auto swapChain = std::make_unique<D3D12SwapChain>(*this);
	if (!swapChain->Init(desc)) return nullptr;
	return MakeRef(std::move(swapChain));
}

// ---------------------------------------------------//
//			コマンドリスト / フェンス / 投入			  //
// ---------------------------------------------------//
RHICommandListRef D3D12Device::CreateCommandList(const char* debugName)
{
	ComPtr<ID3D12CommandAllocator>		allocator;
	ComPtr<ID3D12GraphicsCommandList>	list;
	if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(allocator.GetAddressOf()))) ||
		FAILED(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
			IID_PPV_ARGS(list.GetAddressOf()))))
	{
		return nullptr;
	}

	// 作った直後は記録中なので閉じておく。Begin で Reset して開き直す
	list->Close();

	SetD3D12DebugName(list.Get(), debugName);
	SetD3D12DebugName(allocator.Get(), debugName);

	return MakeRef(std::make_unique<D3D12CommandList>(
		std::move(allocator), std::move(list), m_ShaderVisibleHeap.GetHeap()));
}

RHIFenceRef D3D12Device::CreateFence(uint64_t initialValue)
{
	ComPtr<ID3D12Fence> fence;
	if (FAILED(m_Device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(fence.GetAddressOf()))))
	{
		return nullptr;
	}
	return MakeRef(std::make_unique<D3D12Fence>(std::move(fence)));
}

void D3D12Device::Submit(
	IRHICommandList* const* lists,
	uint32_t count,
	IRHIFence* signalFence,
	uint64_t signalValue)
{
	// 1 回に積むリストは数本なので固定長で足りる
	constexpr uint32_t MAX_LISTS = 16;
	assert(count <= MAX_LISTS);

	ID3D12CommandList* native[MAX_LISTS]{};
	for (uint32_t i = 0; i < count; ++i)
	{
		// D3D12 バックエンドには D3D12 のオブジェクトしか来ないので static_cast でよい
		auto* list = static_cast<D3D12CommandList*>(lists[i]);
		assert(list->IsClosed() && "End していないコマンドリストを Submit した");
		native[i] = list->GetD3D12CommandList();
	}
	if (count > 0)
	{
		m_Queue->ExecuteCommandLists(count, native);
	}

	if (signalFence != nullptr)
	{
		m_Queue->Signal(static_cast<D3D12Fence*>(signalFence)->GetD3D12Fence(), signalValue);
	}
}
