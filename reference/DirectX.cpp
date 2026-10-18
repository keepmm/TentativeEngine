#include "DirectX.hpp"
#include "Vertex.hpp"
#include <algorithm>
#include "d3dx12.h"
#include "Shader.hpp"
#include <DirectXTex.h>
#include "Logger.hpp"
#include <dxgidebug.h>
#include "ShaderTypes.hpp"
#include "Time.hpp"
#include "GpuProfiler.hpp"
#include <d3d12shader.h>
#include <d3dcompiler.h>

#include "ThrowIfFailed.hpp"
bool RunRHISmokeTest();	// RHI/RHISmokeTest.cpp（確認が済んだら消す）

namespace
{
	/// @brief シェーダーの cbuffer Material(b3) が C++ の MaterialCB と一致するか調べる
	/// @note 一致しないと、シェーダーは「ずれた位置」を読むので黙って壊れる。
	///       登録時に見ておけば、宣言を書き間違えてもすぐ気づける
	void ValidateMaterialCB(const D3D12_SHADER_BYTECODE& bytecode, const std::string& name)
	{
		if (bytecode.pShaderBytecode == nullptr) return;

		// C++ 側の並び。ShaderTypes.hpp の MaterialCB と同じ順・同じオフセット
		struct Field { const char* name; UINT offset; };
		static const Field kExpected[] =
		{
			{ "roughness",      0 }, { "metallic",       4 },
			{ "rimColor",      16 }, { "mapFlags",      32 },
			{ "faceParam",     48 }, { "sssParams",     64 },
			{ "sssColor",      80 }, { "basecolor",     96 },
			{ "reflectParam", 112 }, { "pbrParams",    128 },
			{ "emissiveColor",144 },
			{ "waveParams",  160 }, { "waterParams",  176 },
			{ "waveParams2", 192 },
		};

		ComPtr<ID3D12ShaderReflection> refl;
		if (FAILED(D3DReflect(bytecode.pShaderBytecode, bytecode.BytecodeLength,
			IID_PPV_ARGS(&refl))))
		{
			return;   // DXC 製など、反射できない形式は黙って通す
		}

		ID3D12ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("Material");
		D3D12_SHADER_BUFFER_DESC bufDesc{};
		if (cb == nullptr || FAILED(cb->GetDesc(&bufDesc))) return;   // 使っていないシェーダー

		for (const auto& f : kExpected)
		{
			ID3D12ShaderReflectionVariable* v = cb->GetVariableByName(f.name);
			D3D12_SHADER_VARIABLE_DESC vd{};
			if (v == nullptr || FAILED(v->GetDesc(&vd))) continue;   // 宣言が短いだけなら許す

			if (vd.StartOffset != f.offset)
			{
				LOG->LogError("シェーダー '" + name + "' の Material 定数バッファがずれています: "
					+ f.name + " が " + std::to_string(vd.StartOffset)
					+ " バイト目(正しくは " + std::to_string(f.offset)
					+ ")。MaterialCB.hlsli を include していますか?");
			}
		}
	}
}

using ushort = unsigned short;

constexpr float ClearColor[] = {0.0f, 0.5f, 0.7f, 1.0f};

DirectXApp* DirectXApp::s_Instance = nullptr;

DirectXApp::DirectXApp(HWND hWnd, int Window_Width, int Window_Height) :
	m_Window_hWnd(hWnd),
	m_Window_Width(Window_Width),
	m_Window_Height(Window_Height),
	m_Fence_Event(nullptr),
	m_RTV_Handle{}
{
	s_Instance = this;

	UINT FlagsDXGI = 0;
	ID3D12Debug* debug = nullptr;
	HRESULT hr;
#if _DEBUG
	// デバッグレイヤーと DRED は RAM を数百 MB 使う。
	// メモリを測りたいときは環境変数 DX12_NO_DEBUG_LAYER=1 で切れる
	char noDebug[8] = {};
	const bool debugLayer =
		GetEnvironmentVariableA("DX12_NO_DEBUG_LAYER", noDebug, sizeof(noDebug)) == 0 || noDebug[0] != '1';

	if (debugLayer)
	{
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
		{
			dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
		D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
		if (debug) {
			debug->EnableDebugLayer();
			debug->Release();
		}
		FlagsDXGI |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif
	ThrowIfFailed(CreateDXGIFactory2(FlagsDXGI, IID_PPV_ARGS(m_Factory.ReleaseAndGetAddressOf())));

	// ----------------------------------------------//
	//					デバイスの作成				 //
	// ----------------------------------------------//

	//ComPtr<IDXGIAdapter> adapter;
	//ThrowIfFailed(m_Factory->EnumAdapters(0, adapter.GetAddressOf()));

	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<IDXGIFactory6> factory6;

	// DXGI 1.6が使える場合は、GPUのVRAMが最大のアダプタを選ぶ
	if (SUCCEEDED(m_Factory.As(&factory6)))
	{
		for (UINT i = 0;
			SUCCEEDED(factory6->EnumAdapterByGpuPreference(
				i,
				DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())
			));
			++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			// ソフトウェアアダプタ(WARP)は除外
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

			// D3D12 デバイスが作成可能かテスト
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
	}

	// 見つからない場合は従来の方法でアダプタを取得
	if (!adapter)
	{
		for (UINT i = 0; SUCCEEDED(m_Factory->EnumAdapters1(1, adapter.ReleaseAndGetAddressOf())); ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
				_uuidof(ID3D12Device), nullptr)))
			{
				break;
			}
		}
	}

	ThrowIfFailed(D3D12CreateDevice(
		adapter.Get(),
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(m_Device.GetAddressOf())));
#if _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(m_Device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
	{
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->Release();
	}
#endif

	hr = m_Device.As(&m_Device2);


	if (FAILED(hr) || m_Device2 == nullptr)
	{
		m_Device2 = nullptr;
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
	if (SUCCEEDED(m_Device->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS7,
		&options7,
		sizeof(options7)
	)))
	{
		m_MeshShaderSupported = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	}

	// -----------------------------------------------//
	//	  コマンドアロケータとコマンドキューの作成	  //
	// -----------------------------------------------//
	for (int i = 0; i < RTV_NUM; ++i)
	{
		ThrowIfFailed(m_Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(m_CommandAllocator[i].GetAddressOf())));
	}


	// ---------------------------------//
	//		   コマンドキューの作成			//
	// ---------------------------------//
	D3D12_COMMAND_QUEUE_DESC desc_command_queue;
	ZeroMemory(&desc_command_queue, sizeof(desc_command_queue));
	desc_command_queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc_command_queue.Priority = 0;
	desc_command_queue.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_Device->CreateCommandQueue(
		&desc_command_queue,
		IID_PPV_ARGS(m_CommandQueue.GetAddressOf())));

	// GPUタイムスタンプ(失敗しても描画は続ける。プロファイラにGPU行が出ないだけ)
	GpuProfiler::Get().Initialize(m_Device.Get(), m_CommandQueue.Get());

	m_Fence_Event = CreateEvent(NULL, FALSE, FALSE, NULL);
	ThrowIfFailed(m_Device->CreateFence(
		0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(m_Fence.GetAddressOf())));
	// --------------------------------------//
	//			スワップチェーンの作成		 //
	// --------------------------------------//
	DXGI_SWAP_CHAIN_DESC desc_swap_chain;
	ZeroMemory(&desc_swap_chain, sizeof(desc_swap_chain));
	desc_swap_chain.BufferCount = RTV_NUM;
	desc_swap_chain.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc_swap_chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc_swap_chain.OutputWindow = m_Window_hWnd;
	desc_swap_chain.SampleDesc.Count = 1;
	desc_swap_chain.Windowed = TRUE;
	desc_swap_chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc_swap_chain.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	ThrowIfFailed(m_Factory->CreateSwapChain(
		m_CommandQueue.Get(),
		&desc_swap_chain,
		(IDXGISwapChain**)m_SwapChain.GetAddressOf()));

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	for (int i = 0; i < RTV_NUM; ++i)
	{
		m_FenceValue[i] = 0;
	}
#ifdef _FRAMEPIPELINE
	m_SyncPacked.store((0ull << 8) | m_SwapChain->GetCurrentBackBufferIndex());

	m_GpuExecThread = std::thread([this] {GpuExecThreadMain(); });

	for (int i = 0; i < RTV_NUM; ++i)
	{
		ThrowIfFailed(m_Device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_CommandAllocator[i].Get(), nullptr,
			IID_PPV_ARGS(m_CommandList[i].GetAddressOf())));

		m_CommandList[i].As(&m_CommandList6[i]);

		ThrowIfFailed(m_CommandList[i]->Close());
	}
#else

	ThrowIfFailed(m_Device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_CommandAllocator[m_FrameIndex].Get(),
		nullptr,
		IID_PPV_ARGS(m_CommandList.GetAddressOf())
	);

	m_CommandList.As(&m_CommandList6);

	hr = m_CommandList->Close();
	if (FAILED(hr)) {
		return;
	}
#endif

	// ディスクリプタヒープの作成
	// RenderTargetView を3つ
	m_RtvAllocator.Init(
		m_Device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		64,
		false);

	UINT strideHandleBytes = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT i = 0; i < desc_swap_chain.BufferCount; i++) {
		m_SwapChain->GetBuffer(
			i,
			IID_PPV_ARGS(m_RenderTargets[i].GetAddressOf())
		);

		UINT rtvIndex = 0;
		m_RtvAllocator.Allocate(rtvIndex);	// RTVスロットを確保

		m_RTV_Handle[i] = m_RtvAllocator.Cpu(rtvIndex);	// RTVのCPUハンドルを取得
		m_Device->CreateRenderTargetView(
			m_RenderTargets[i].Get(),
			nullptr,
			m_RTV_Handle[i]
		);

		m_RenderTargets[i]->SetName(L"BackBuffer");
	}

	// ------------------------------------//
	//	 深度バッファと DSV ヒープの作成   //
	// ------------------------------------//
	m_DsvAllocator.Init(m_Device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		16,
		false);

	CD3DX12_RESOURCE_DESC depthResDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32_TYPELESS, 
		m_Window_Width, m_Window_Height,
		1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	// ClearValueは実フォーマットで指定
	CD3DX12_CLEAR_VALUE DepthClearValue(DXGI_FORMAT_D32_FLOAT, 1.0f, 0);

	CD3DX12_HEAP_PROPERTIES depthHeapProp(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(m_Device->CreateCommittedResource(
		&depthHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&depthResDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&DepthClearValue,
		IID_PPV_ARGS(m_Depthbuffer.GetAddressOf())));

	// DSVスロットを確保して深度バッファ生成
	UINT dsvIndex = 0;
	m_DsvAllocator.Allocate(dsvIndex);
	m_DSV_Handle = m_DsvAllocator.Cpu(dsvIndex);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format				= DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension		= D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags				= D3D12_DSV_FLAG_NONE;
	m_Device->CreateDepthStencilView(
		m_Depthbuffer.Get(),
		&dsvDesc,
		m_DSV_Handle
	);

	// =======================================================
	//		グローバル SRV ヒープ（CBV/SRV/UAV・256スロット）
	// ======================================================
	m_SrvAllocator.Init(
		m_Device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		256,
		true
	);

	// 深度SRV
	UINT depthdsvIndex = 0;
	m_SrvAllocator.Allocate(depthdsvIndex);
	m_DepthSrvHandleCpu = m_SrvAllocator.Cpu(depthdsvIndex);
	m_DepthSrvHandleGpu = m_SrvAllocator.Gpu(depthdsvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
	depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSrvDesc.Texture2D.MipLevels = 1;
	m_Device->CreateShaderResourceView(
		m_Depthbuffer.Get(),
		&depthSrvDesc,
		m_DepthSrvHandleCpu
	);

	CreateRootSignature();
	CreateDeferredRootSignature();
	CreatePipelineStateObject();

	m_ShadowMap.Init();

	RunRHISmokeTest();	// RHI の動作確認（確認が済んだら消す）

	return;
}

bool DirectXApp::ReloadShader()
{
	if (!m_ShaderLibrary.ReoadChanged())
	{
		return false;
	}

	WaitForGPUIdle();
	m_PsoCache.Clear();
	CreatePipelineStateObject();
	return true;
}

bool DirectXApp::CreateShadeFromSource(const std::string& name, const std::string& hlslCode, std::string& psEntry, std::string& outError, bool alphaBlend)
{
	namespace fs = std::filesystem;
	fs::create_directories(L"Assets/Shaders");
	const std::wstring& path = L"Assets/Shaders/" + std::wstring(name.begin(), name.end()) + L".hlsl";

	// 1,保存
	std::ofstream ofs(path, std::ios::binary);
	ofs << hlslCode;

	//2 強制コンパイル
	const Shader* ps = m_ShaderLibrary.Reload(path, psEntry, "ps_5_0", outError);
	if (ps == nullptr) return false;

	// 3 PSO登録
	WaitForGPUIdle();
	ShaderPassDef def;
	def.psFile = path;
	def.psEntry = psEntry;
	def.alphaBlend = alphaBlend;

}

void DirectXApp::DeferredLightingPass(const RenderContext& ctx,
	D3D12_CPU_DESCRIPTOR_HANDLE targetRtv,
	const D3D12_VIEWPORT& vp, const D3D12_RECT& sc)
{
	auto* cmd = Cmd();
	const UINT slot = RecordSlot();

	// G-Buffer + 深度を SRV へ
	m_GBuffer.TransitionToRead(cmd);
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	// 出力先（ビューポートRT, DSVなし）
	cmd->OMSetRenderTargets(1, &targetRtv, FALSE, nullptr);
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	const float clr[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmd->ClearRenderTargetView(targetRtv, clr, 0, nullptr);

	cmd->SetGraphicsRootSignature(m_DeferredRootSignature.Get());
	cmd->SetPipelineState(m_DeferredPso.Get());

	// b0 Frame
	FrameCB fdata{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		XMStoreFloat4x4(&fdata.viewProj, XMMatrixTranspose(v * p));
		const auto invV = XMMatrixInverse(nullptr, v);
		float4x4 iv; XMStoreFloat4x4(&iv, invV);
		// w には経過秒を入れる(水面の波などが時間を使う)
		fdata.cameraPos = { iv._41, iv._42, iv._43, TIME->GetTotalTime() };
	}
	const auto b0 = m_CBAllocator.Allocate(slot, &fdata, sizeof(fdata));

	// b2 Light
	const auto b2 = m_CBAllocator.Allocate(slot, &ctx.lightCb, sizeof(LightCB));

	// b3 invViewProj
	DeferredCB dcb{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		const auto inv = XMMatrixInverse(nullptr, v * p);
		XMStoreFloat4x4(&dcb.invViewProj, XMMatrixTranspose(inv));
		dcb.envParam = { HasEnvironment() ? (float)(m_EnvMipLevels - 1) : 0.0f, 0.5f, 0.15f, 1.0f };
	}
	const auto b3 = m_CBAllocator.Allocate(slot, &dcb, sizeof(dcb));

	if (b0 == 0 || b2 == 0 || b3 == 0) return;

	cmd->SetGraphicsRootConstantBufferView(0, b0);
	cmd->SetGraphicsRootConstantBufferView(1, b2);
	cmd->SetGraphicsRootConstantBufferView(2, b3);
	cmd->SetGraphicsRootDescriptorTable(3, m_GBuffer.GetSrvTableStart());

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); 
	cmd->DrawInstanced(3, 1, 0, 0);

	bool hasVolumetric = RenderSettings::Get().volumetric;
	bool hasAny = false;
	UINT volCount = 0;
	for (int i = 0; i < (int)ctx.lightCb.lightCount.x; ++i)
	{
		const auto& l = ctx.lightCb.lights[i];
		if ((int)l.param.x >= 2 && l.param.w > 0.0f)
		{
			hasAny = true;
			++volCount;
		}
	}
	m_LastLightCount = (UINT)ctx.lightCb.lightCount.x;
	m_LastVolumetricCount = volCount;
	hasVolumetric = hasVolumetric && hasAny;


	if(hasVolumetric)
	{
		GPU_PROFILE_SCOPE(cmd, "Draw/Volumetric");

		// ---- ボリュームライト: ハーフ解像度で描いて加算アップサンプル ----
		const UINT hw = m_Window_Width / VOLUMETRIC_DIV, hh = m_Window_Height / VOLUMETRIC_DIV;

		m_VolumetricHalf.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
		auto volRtv = m_VolumetricHalf.GetRTV();
		const float zero[4] = { 0,0,0,0 };
		cmd->ClearRenderTargetView(volRtv, zero, 0, nullptr);
		cmd->OMSetRenderTargets(1, &volRtv, FALSE, nullptr);
		D3D12_VIEWPORT hvp{ 0,0,(float)hw,(float)hh,0,1 };
		D3D12_RECT     hsc{ 0,0,(LONG)hw,(LONG)hh };
		cmd->RSSetViewports(1, &hvp);
		cmd->RSSetScissorRects(1, &hsc);
		cmd->SetPipelineState(m_VolumetricPso.Get());   // 既存のレイマーチPSO(加算→0クリア上なので実質上書き)
		cmd->DrawInstanced(3, 1, 0, 0);

		// ハーフRT → HDR へ加算アップサンプル(バイリニアで滑らかに拡大)
		m_VolumetricHalf.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		FrameCB dummy{};
		auto cbDummy = m_CBAllocator.Allocate(slot, &dummy, sizeof(dummy));
		PostPass(m_VolumetricAddPso.Get(), m_VolumetricHalf.GetSRV(), cbDummy,
			targetRtv, (UINT)vp.Width, (UINT)vp.Height);
	}

	// 深度を書き戻し
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void DirectXApp::PostProcessCopy(RenderTexture& src,
	D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
	const D3D12_VIEWPORT& vp, const D3D12_RECT& sc)
{
	auto* cmd = Cmd();

	// src(HDR) を SRV へ遷移
	src.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// 直前パスでマテリアル自前ヒープが束縛されている可能性があるのでグローバルへ戻す
	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	// 出力先（DSVなし）
	cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	cmd->SetGraphicsRootSignature(m_PostRootSignature.Get());
	cmd->SetPipelineState(m_CopyPso.Get());

	cmd->SetGraphicsRootDescriptorTable(0, src.GetSRV());

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
}

void DirectXApp::PostProcessBloom(D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, const D3D12_VIEWPORT& vp, const D3D12_RECT& sc)
{
	auto* cmd = Cmd();
	const UINT slot = RecordSlot();
	const UINT bw = m_Window_Width / 2, bh = m_Window_Height / 2;

	PostCB pcb{};
	pcb.threshold = 1.0f;   // 1.0超（＝HDR高輝度）を抽出
	pcb.intensity = 1.0f;

	m_HdrScene.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	auto cb1 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	PostPass(m_BrightPso.Get(), m_HdrScene.GetSRV(), cb1, m_BloomA.GetRTV(), bw, bh);

	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomB.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	pcb.texelSize = { 1.0f / bw, 0.0f };
	auto cb2 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	PostPass(m_BlurPso.Get(), m_BloomA.GetSRV(), cb2, m_BloomB.GetRTV(), bw, bh);

	m_BloomB.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	pcb.texelSize = { 0.0f, 1.0f / bh };
	auto cb3 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	PostPass(m_BlurPso.Get(), m_BloomB.GetSRV(), cb3, m_BloomA.GetRTV(), bw, bh);

	m_HdrScene.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	auto cb4 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->SetGraphicsRootSignature(m_PostRootSignature.Get());
	cmd->SetPipelineState(m_CompositePso.Get());
	cmd->SetGraphicsRootDescriptorTable(0, m_CompositeSrvStart);
	cmd->SetGraphicsRootConstantBufferView(1, cb4);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
}

ID3D12PipelineState* DirectXApp::RegisterShaderPass(const std::string& name, const ShaderPassDef& def)
{
	const Shader* vs = m_ShaderLibrary.Load(def.vsFile, def.vsEntry, def.vsProfile);
	const Shader* ps = m_ShaderLibrary.Load(def.psFile, def.psEntry, def.psProfile);
	if (vs == nullptr || ps == nullptr)
	{
		return nullptr;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakeBasePsoDesc();
	desc.VS = vs->GetByteCode();
	desc.PS = ps->GetByteCode();

	// 宣言の食い違いは黙って壊れるので、登録時に見ておく
	ValidateMaterialCB(desc.PS, name);
	desc.RasterizerState.CullMode = def.cullMode;
	desc.RTVFormats[0] = def.rtvFormat;
	if (def.alphaBlend)
	{
		auto& rt = desc.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
	}

	auto pso = m_PsoCache.GetOrCreate(name, m_Device.Get(), desc);
	if (pso == nullptr)
	{
		return nullptr;
	}

	m_ShaderRegistry[name] = { def,pso };
	return pso.Get();
}

bool DirectXApp::HasShaderPass(const std::string& name) const
{
	return m_ShaderRegistry.find(name) != m_ShaderRegistry.end();
}

ID3D12PipelineState* DirectXApp::GetPipelineStateByName(const std::string& name) const
{
	auto it = m_ShaderRegistry.find(name);
	if (it != m_ShaderRegistry.end())
	{
		return it->second.pso.Get();
	}
	return nullptr;
}

std::vector<std::string> DirectXApp::GetShaderNames() const
{
	std::vector<std::string> names;
	names.reserve(m_ShaderRegistry.size());
	for (auto& [name, _] : m_ShaderRegistry)
	{
		names.push_back(name);
	}
	return names;
}



void DirectXApp::CreateDeferredRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7, 0);

	CD3DX12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0
	rootParameters[1].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);	// b2
	rootParameters[2].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);	// b3
	rootParameters[3].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL); // t0~t6

	CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc[3] = {
	CD3DX12_STATIC_SAMPLER_DESC(
	0,	// shaderRegister : s0
	D3D12_FILTER_MIN_MAG_MIP_LINEAR,
	D3D12_TEXTURE_ADDRESS_MODE_WRAP,
	D3D12_TEXTURE_ADDRESS_MODE_WRAP,
	D3D12_TEXTURE_ADDRESS_MODE_WRAP
	),

CD3DX12_STATIC_SAMPLER_DESC(
	1,	// shaderRegister : s1
	D3D12_FILTER_MIN_MAG_MIP_POINT,
	D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
	D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
	D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	),

	[] {
		CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
			2,
			D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		return shadowSampler;
	}()
	};

	CD3DX12_ROOT_SIGNATURE_DESC desc;
	desc.Init(
		_countof(rootParameters),rootParameters,
		_countof(staticSamplerDesc),staticSamplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	ComPtr<ID3DBlob> sig, err;
	if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err)))
	{
		return;
	}

	m_Device->CreateRootSignature(
		0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(m_DeferredRootSignature.GetAddressOf())
	);
}

void DirectXApp::CreatePostRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srv;
	// t0:入力 t1:ブルーム/ぼかし t2:深度(被写界深度で使う)
	srv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
	CD3DX12_ROOT_PARAMETER rp[2];
	rp[0].InitAsDescriptorTable(1, &srv, D3D12_SHADER_VISIBILITY_PIXEL);
	rp[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);	// b0

	CD3DX12_STATIC_SAMPLER_DESC samp(0,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	);

	CD3DX12_ROOT_SIGNATURE_DESC desc;
	desc.Init(_countof(rp), rp, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> sig, err;
	    D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    m_Device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(m_PostRootSignature.GetAddressOf()));
}

void DirectXApp::CreateRootSignature()
{

	// t7 は rootParameters[6] の頂点側SRV(ボーン)が使っているので、
	// 反射だけレンジを分けて t8 に置く
	CD3DX12_DESCRIPTOR_RANGE srvRange[3] = {};
	srvRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);   // t0~t7（t7 = エミッシブ）
	srvRange[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);   // t8  = 平面反射
	srvRange[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9);   // t9  = オクルージョン
	// t0 albedo
	// t1 toon ramp
	// t2 normal
	// t3 metal
	// t4 roughness
	// t5 環境
	// t6 シャドウマップ
	// t8 平面反射

	// 配列の数がそのまま定数バッファやSRVの数になる
	CD3DX12_ROOT_PARAMETER rootParameters[7] = {};
	// b0 ~ b3 にCBVを割り当てる
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[3].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_ALL);

	// t0 にSRVを割り当てる
	rootParameters[4].InitAsDescriptorTable(_countof(srvRange), srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[5].InitAsConstantBufferView(4,0,D3D12_SHADER_VISIBILITY_VERTEX);
	rootParameters[6].InitAsShaderResourceView(7,0,D3D12_SHADER_VISIBILITY_VERTEX);


	CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc[3] = {
		CD3DX12_STATIC_SAMPLER_DESC(
		0,	// shaderRegister : s0
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
		),

	CD3DX12_STATIC_SAMPLER_DESC(
		1,	// shaderRegister : s1
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
		),

		[] {
			CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
				2, 
				D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
				D3D12_TEXTURE_ADDRESS_MODE_BORDER, 
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,
				D3D12_TEXTURE_ADDRESS_MODE_BORDER);
			shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
			return shadowSampler;
		}()
	};

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		_countof(rootParameters),
		rootParameters,
		_countof(staticSamplerDesc),
		staticSamplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> SerializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;


	auto result = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&SerializedRootSig,
		&errorBlob);
	if (FAILED(result))return;

	m_Device->CreateRootSignature(
		0,
		SerializedRootSig->GetBufferPointer(),
		SerializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(m_rootSignature.GetAddressOf())
	);
}

void DirectXApp::CreatePipelineStateObject()
{
	D3D12_INPUT_ELEMENT_DESC InputLayout[] =
	{
		{"Position",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"Normal",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}, 
		{"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,60,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,76,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};

	const Shader* vs = m_ShaderLibrary.Load(L"VertexShader.hlsl", "BasicVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"PixelShader.hlsl", "BasicPS", "ps_5_0");
	const Shader* toon = m_ShaderLibrary.Load(L"ToonShader.hlsl", "ToonPS", "ps_5_0");
	const Shader* wirePs = m_ShaderLibrary.Load(L"PixelShader.hlsl", "WireFramePS", "ps_5_0");
	const Shader* lineVS = m_ShaderLibrary.Load(L"PS_LineShader.hlsl", "LineVS", "vs_5_0");
	const Shader* linePS = m_ShaderLibrary.Load(L"PS_LineShader.hlsl", "LinePS", "ps_5_0");
	const Shader* iconPS = m_ShaderLibrary.Load(L"PixelShader.hlsl", "unlitPS", "ps_5_0");
	const Shader* skyVS = m_ShaderLibrary.Load(L"SkyBoxShader.hlsl", "SkyboxVS", "vs_5_0");
	const Shader* skyPS = m_ShaderLibrary.Load(L"SkyBoxShader.hlsl", "SkyboxPS", "ps_5_0");
	const Shader* shadowVS = m_ShaderLibrary.Load(L"ShadowShader.hlsl", "ShadowVS", "vs_5_0");
	const Shader* BeamVS = m_ShaderLibrary.Load(L"LaserBeamShader.hlsl", "LaserVS", "vs_5_0");
	const Shader* BeamPS = m_ShaderLibrary.Load(L"LaserBeamShader.hlsl", "LaserPS", "ps_5_0");

	if (vs == nullptr || ps == nullptr || toon == nullptr || 
		wirePs == nullptr || lineVS == nullptr || linePS == nullptr || 
		skyPS == nullptr || skyVS == nullptr || shadowVS == nullptr || BeamVS == nullptr || BeamPS == nullptr)
	{
		assert(false);
		return;
	}

	D3D12_INPUT_ELEMENT_DESC linelayout[] =
	{
		{"Position",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { InputLayout, _countof(InputLayout) };
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = vs->GetByteCode();
	psoDesc.PS = ps->GetByteCode();
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;

	const size_t vsIndex = static_cast<size_t>(E_VERTEX_SHADER::BASIC);
	const size_t psIndex = static_cast<size_t>(E_PIXEL_SHADER::BASIC);
	const size_t psToonIndex = static_cast<size_t>(E_PIXEL_SHADER::TOON);


	RegisterBuiltinShaders();

	auto wireDesc = psoDesc;
	wireDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	wireDesc.PS = wirePs->GetByteCode();


	m_pipelineStateWireFrame = m_PsoCache.GetOrCreate("BasicVS_WireFramePS",
		m_Device.Get(),
		wireDesc
	);
	if(m_pipelineStateWireFrame == nullptr) {
		assert(false);
	}

	auto lineDesc = psoDesc;
	lineDesc.VS = lineVS->GetByteCode();
	lineDesc.PS = linePS->GetByteCode();
	lineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	lineDesc.InputLayout = { linelayout,_countof(linelayout) };
	lineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	lineDesc.DepthStencilState.DepthEnable = FALSE;
	m_LinePso = m_PsoCache.GetOrCreate("LineVS_LinePS",
		m_Device.Get(),
		lineDesc
	);
	if (m_LinePso == nullptr) {
		assert(false);

	}
	auto lineDepthDesc = lineDesc;
	lineDepthDesc.DepthStencilState.DepthEnable = TRUE;
	lineDepthDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	lineDepthDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	m_LineDepthPso = m_PsoCache.GetOrCreate("LineVS_LinePS_Depth",
		m_Device.Get(),
		lineDepthDesc
	);
	if (m_LineDepthPso == nullptr) {
		assert(false);
	}

	// HDRパス用（RTフォーマットが違うと PSO は使い回せない）
	auto lineHdrDepthDesc = lineDepthDesc;
	lineHdrDepthDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_LineHdrDepthPso = m_PsoCache.GetOrCreate("LineVS_LinePS_DepthHdr",
		m_Device.Get(),
		lineHdrDepthDesc
	);
	if (m_LineHdrDepthPso == nullptr) {
		assert(false);
	}

	auto BeamDesc = lineDesc;
	BeamDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	BeamDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	auto& brt = BeamDesc.BlendState.RenderTarget[0];
	// アルファブレンド有効
	brt.BlendEnable				= TRUE;
	brt.SrcBlend				= D3D12_BLEND_SRC_ALPHA;
	brt.DestBlend				= D3D12_BLEND_ONE;
	brt.BlendOp					= D3D12_BLEND_OP_ADD;
	// アルファは触らない。ここを ONE/ZERO にすると PS が返した芯の値(縁で0)で
	// レンダーターゲットのアルファが上書きされ、ImGui表示時にビーム板の形に透ける
	brt.SrcBlendAlpha			= D3D12_BLEND_ZERO;
	brt.DestBlendAlpha			= D3D12_BLEND_ONE;
	brt.BlendOpAlpha			= D3D12_BLEND_OP_ADD;
	brt.RenderTargetWriteMask	= D3D12_COLOR_WRITE_ENABLE_ALL;

	BeamDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	BeamDesc.DepthStencilState.DepthEnable = FALSE;

	BeamDesc.VS = BeamVS->GetByteCode();
	BeamDesc.PS = BeamPS->GetByteCode();

	// --- R8用（forward経路 / BeamRenderer.Init に渡すPSO）---
	m_BeamPso = m_PsoCache.GetOrCreate("BeamVS_BeamPS",
		m_Device.Get(),
		BeamDesc
	);
	if (m_BeamPso == nullptr) {
		assert(false);
	}

	// --- HDR用（deferredのBloom前描画）---
	auto BeamHdrDesc = BeamDesc;
	BeamHdrDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_BeamHdrPso = m_PsoCache.GetOrCreate("BeamVS_BeamPS_HDR",
		m_Device.Get(),
		BeamHdrDesc
	);
	if (m_BeamHdrPso == nullptr) {
		assert(false);
	}
	const Shader* FireworkPS = m_ShaderLibrary.Load(L"LaserBeamShader.hlsl", "FireworkPS", "ps_5_0");
	auto FireworkDesc = BeamDesc;
	FireworkDesc.PS = FireworkPS->GetByteCode();
	auto& frt = FireworkDesc.BlendState.RenderTarget[0];
	frt.SrcBlend = D3D12_BLEND_ONE;   // PS側でαを乗算済み(pre-multiplied)
	frt.DestBlend = D3D12_BLEND_ONE;
	m_FireworkPso = m_PsoCache.GetOrCreate("BeamVS_FireworkPS",
		m_Device.Get(), FireworkDesc);
	if (m_FireworkPso == nullptr) { assert(false); }


	auto iconDesc = psoDesc;                 // BASICベース
	iconDesc.PS = iconPS->GetByteCode();

	// アルファブレンド有効
	auto& rt = iconDesc.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	iconDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	m_IconPso = m_PsoCache.GetOrCreate("IconPSO", m_Device.Get(), iconDesc);
	if (m_IconPso == nullptr) { assert(false); }

	// ---------------------------------- //
	//			UI用のPSOを作成			  //
	// ---------------------------------- //
	auto uiDesc = iconDesc;
	auto& uirt = uiDesc.BlendState.RenderTarget[0];
	uirt.BlendEnable = TRUE;
	uirt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	uirt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	uirt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	uirt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	uiDesc.DepthStencilState.DepthEnable = FALSE;
	uiDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	uiDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	m_UIPso = m_PsoCache.GetOrCreate("UIPSO", m_Device.Get(), uiDesc);
	if (m_UIPso == nullptr) { assert(false); }

	// ---------------------------------- //
	//			SkyBox用のPSOを作成		  //
	// ---------------------------------- //
	auto skyDesc = psoDesc;
	skyDesc.VS = skyVS->GetByteCode();
	skyDesc.PS = skyPS->GetByteCode();
	skyDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	m_SkyPso = m_PsoCache.GetOrCreate("SkyBoxPSO", m_Device.Get(), skyDesc);
	if (m_SkyPso == nullptr) { assert(false); }

	// --- HDR用（deferredのBloom前描画）---
	auto skyHdrDesc = skyDesc;
	skyHdrDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_SkyHdrPso = m_PsoCache.GetOrCreate("SkyBoxPSO_HDR", m_Device.Get(), skyHdrDesc);
	if (m_SkyHdrPso == nullptr) { assert(false); }

	// ---------------------------------- //
	// 		Shadow用のPSOを作成			  //
	// ---------------------------------- //
	auto sd = MakeBasePsoDesc();
	sd.VS = shadowVS->GetByteCode();
	sd.PS = D3D12_SHADER_BYTECODE{ nullptr, 0 };   // PSなし
	sd.NumRenderTargets = 0;
	sd.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	sd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	sd.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	sd.RasterizerState.DepthBias = 10000;               // ピーターパン/アクネ対策
	sd.RasterizerState.SlopeScaledDepthBias = 1.5f;
	m_ShadowPso = m_PsoCache.GetOrCreate("ShadowPSO", m_Device.Get(), sd);

	CreatePostRootSignature();

	CreateGbufferPSO();
	CreateDeferredPSO();
	CreatePostPSO();
	CreateCopyPSO();
	m_CBAllocator.Init();

	m_GBuffer.Init(
		m_Window_Width, m_Window_Height,
		m_Depthbuffer.Get(),          // 深度リソース
		m_EnvTexture.Get(),           // null可
		m_ShadowMap.GetResource(),
		m_EnvMipLevels);   // シャドウリソース

	m_HdrScene.Init(m_Window_Width, m_Window_Height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	UINT bw = m_Window_Width, bh = m_Window_Height;
	m_BloomA.Init(bw, bh, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_BloomB.Init(bw, bh, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_VolumetricHalf.Init(m_Window_Width / VOLUMETRIC_DIV, m_Window_Height / VOLUMETRIC_DIV,
		DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_HdrScene.GetResource()->SetName(L"HDRScene");
	m_BloomA.GetResource()->SetName(L"BloomA");
	m_BloomB.GetResource()->SetName(L"BloomB");

	CreateBloomPSOs();
	CreateDofPSOs();
	CreateVolumetricPSO();
	BuildCompositeSrvTable();
}

#include "Util.hpp"

void DirectXApp::RegisterBuiltinShaders()
{
	// パスの解決は ShaderLibrary::Load 側(ResolveShaderPath)に任せる。
	// ここは従来どおりファイル名だけを書く
	struct Entry { const char* name; ShaderPassDef def; };
	const Entry builtins[] = {
		{ "Basic", { L"VertexShader.hlsl","BasicVS","vs_5_0", L"PixelShader.hlsl","BasicPS","ps_5_0", false } },
		{ "Toon",  { L"VertexShader.hlsl","BasicVS","vs_5_0", L"ToonShader.hlsl", "ToonPS", "ps_5_0", false } },
		{ "Unlit", { L"VertexShader.hlsl","BasicVS","vs_5_0", L"PixelShader.hlsl","unlitPS","ps_5_0", true  } },
		{ "PBR",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"PBRShader.hlsl",  "PbrPS",  "ps_5_0", false } },
		{ "Rim",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"RimShader.hlsl",  "RimPS",  "ps_5_0", false } },
		{ "Fresnel",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"FresnelShader.hlsl",  "FresnelPS",  "ps_5_0", false } },
		{ "Dissolve",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"DissolveShader.hlsl",  "DissolvePS",  "ps_5_0", false } },
		{ "BlinnPhong",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"BlinnPhongShader.hlsl",  "PhongPS",  "ps_5_0", false } },
		{ "Glass",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"GlassShader.hlsl",  "GlassPS",  "ps_5_0", true } },
		{ "Water",   { L"WaterShader.hlsl","WaterVS","vs_5_0", L"WaterShader.hlsl",  "WaterPS",  "ps_5_0", true } },

		{ "SkinnedPBR", { L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"PBRShader.hlsl","PbrPS","ps_5_0", false } },
		{ "SkinnedToon",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"ToonShader.hlsl","ToonPS","ps_5_0", false } },
		// 平面反射パス用。鏡像はワインディングが反転するのでカリングを裏返す
		{ "SkinnedToonMirror",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"ToonShader.hlsl","ToonPS","ps_5_0",
					   false, D3D12_CULL_MODE_FRONT } },
		{ "SkinnedUnlit",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"PixelShader.hlsl","unlitPS","ps_5_0", true } },
		{ "SkinnedRim",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"RimShader.hlsl","RimPS","ps_5_0", false } },
		{ "SkinnedFresnel",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"FresnelShader.hlsl","FresnelPS","ps_5_0", false } },
		{ "SkinnedDissolve",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"DissolveShader.hlsl","DissolvePS","ps_5_0", false } },
		{ "SkinnedBlinnPhong",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"BlinnPhongShader.hlsl","PhongPS","ps_5_0", false } },
		{ "SkinnedGlass",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"GlassShader.hlsl","GlassPS","ps_5_0", true } },


		{ "Genshin_Toon",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"Genshin_ToonShader.hlsl","Genshin_ToonPS","ps_5_0", false } },
		{ "Genshin_Outline", { L"GenshinOutline.hlsl","Genshin_OutlineVS","vs_5_0",
					   L"GenshinOutline.hlsl","Genshin_OutlinePS","ps_5_0",
					   false, D3D12_CULL_MODE_FRONT } },
	};
	for (auto& e : builtins)
	{
		RegisterShaderPass(e.name, e.def);

		// HDRシーン(R16F)へ直接描く双子を同時に登録する。
		// フォワードのトゥーン/半透明を Bloom より前に描くために使う。
		// これを通さないとキャラだけトーンマップもブルームも掛からず、
		// 背景から浮いた切り抜きになる
		ShaderPassDef hdr = e.def;
		hdr.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
		RegisterShaderPass(std::string(e.name) + HDR_PASS_SUFFIX, hdr);
	}
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC DirectXApp::MakeBasePsoDesc() const
{
	static const D3D12_INPUT_ELEMENT_DESC layout[] =
	{
		{"Position",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"Normal",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{ "BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,60,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 },
		{"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,76,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.InputLayout = { layout, _countof(layout) };
	desc.pRootSignature = m_rootSignature.Get();
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.SampleMask = UINT_MAX;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	desc.SampleDesc.Count = 1;
	return desc;
}

bool DirectXApp::CreateEnvTextureFromImage(const DirectX::ScratchImage& image)
{
	const DirectX::TexMetadata& meta = image.GetMetadata();
	if (meta.mipLevels == 0) return false;

	CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
		meta.format,
		static_cast<UINT64>(meta.width),
		static_cast<UINT>(meta.height),
		1,
		static_cast<UINT16>(meta.mipLevels));

	CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(m_Device->CreateCommittedResource(
		&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(m_EnvTexture.ReleaseAndGetAddressOf()))))
		return false;

	std::vector<D3D12_SUBRESOURCE_DATA> subs(meta.mipLevels);
	for (size_t m = 0; m < meta.mipLevels; ++m)
	{
		const DirectX::Image* im = image.GetImage(m, 0, 0);
		if (im == nullptr) return false;
		subs[m].pData = im->pixels;
		subs[m].RowPitch = static_cast<LONG_PTR>(im->rowPitch);
		subs[m].SlicePitch = static_cast<LONG_PTR>(im->slicePitch);
	}

	const UINT64 uploadSize = GetRequiredIntermediateSize(
		m_EnvTexture.Get(), 0, static_cast<UINT>(meta.mipLevels));

	ComPtr<ID3D12Resource> upload;
	CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	if (FAILED(m_Device->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(upload.GetAddressOf()))))
		return false;

	// ロード時なので使い捨てのリストで転送して待つ
	ComPtr<ID3D12CommandAllocator> alloc;
	ComPtr<ID3D12GraphicsCommandList> list;
	if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
		FAILED(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list))))
		return false;

	UpdateSubresources(list.Get(), m_EnvTexture.Get(), upload.Get(),
		0, 0, static_cast<UINT>(meta.mipLevels), subs.data());

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_EnvTexture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	list->ResourceBarrier(1, &barrier);
	list->Close();

	ID3D12CommandList* lists[] = { list.Get() };
	m_CommandQueue->ExecuteCommandLists(1, lists);
	WaitForGPUIdle();

	++m_EnvGeneration;
	return true;
}

void DirectXApp::CreateGbufferPSO()
{
	auto desc = MakeBasePsoDesc();

	const Shader* vs = m_ShaderLibrary.Load(L"SkinnedShader.hlsl", "SkinnedVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"GBufferShader.hlsl", "GBufferPS", "ps_5_0");
	desc.VS = vs->GetByteCode();
	desc.PS = ps->GetByteCode();

	if (!vs || !ps) { OutputDebugStringA("GBuffer shader load failed\n"); assert(false); return; }

	desc.NumRenderTargets = GBuffer::RT_COUNT;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;// Albedo
	desc.RTVFormats[1] = DXGI_FORMAT_R10G10B10A2_UNORM;  // Normal
	desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;     // ORM(b = AO)
	desc.RTVFormats[3] = DXGI_FORMAT_R11G11B10_FLOAT;    // Emissive
	desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	// 深度は「書き込みあり・テストあり」（不透明を前から手前判定）
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_GBufferPso.GetAddressOf()));
}

void DirectXApp::CreateDeferredPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_DeferredRootSignature.Get();

	const Shader* vs = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "DeferredPS", "ps_5_0");

	if (!vs || !ps) { OutputDebugStringA("Deferred shader load failed\n"); assert(false); return; }

	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();

	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_DeferredPso.GetAddressOf()));
}

void DirectXApp::CreatePostPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_PostRootSignature.Get();
	const Shader* vs = m_ShaderLibrary.Load(L"PostProcess.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"PostProcess.hlsl", "CopyPS", "ps_5_0");
	if (!vs || !ps) { OutputDebugStringA("PostProcess shader load failed\n"); assert(false); return; }
	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();
	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_PostPso.GetAddressOf()));
}

void DirectXApp::CreateCopyPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_PostRootSignature.Get();

	const Shader* vs = m_ShaderLibrary.Load(L"PostProcess.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"PostProcess.hlsl", "CopyPS", "ps_5_0");

	if (!vs || !ps) { OutputDebugStringA("PostProcess shader load failed\n"); assert(false); return; }

	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();

	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_CopyPso.GetAddressOf()));

	//ボリューム加算
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC ad = desc;
		ad.pRootSignature = m_PostRootSignature.Get();
		ad.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		auto& rt = ad.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_ONE;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		HRESULT hr = m_Device->CreateGraphicsPipelineState(&ad, IID_PPV_ARGS(m_VolumetricAddPso.GetAddressOf()));
		assert(SUCCEEDED(hr) && m_VolumetricAddPso);
	}
}

void DirectXApp::CreateVolumetricPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_DeferredRootSignature.Get();

	const Shader* vs = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "VolumetricPS", "ps_5_0");

	if (!vs || !ps) { OutputDebugStringA("Volumetrics shader load failed\n"); assert(false); return; }

	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();

	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

	D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_ONE;   // 加算合成
	rt.DestBlend = D3D12_BLEND_ONE;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_VolumetricPso.GetAddressOf()));
}

void DirectXApp::CreateShadowSrv()
{
	auto* sm = m_ShadowMap.GetResource();
	if (!sm)return;

	m_SrvAllocator.Allocate(m_ShadowSrvIndex);
	m_ShadowSrvCpu = m_SrvAllocator.Cpu(m_ShadowSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	m_Device->CreateShaderResourceView(sm, &srv, m_ShadowSrvCpu);
}

void DirectXApp::CreateEnvSrv()
{
	if (!m_EnvTexture) return;

	m_SrvAllocator.Allocate(m_EnvSrvIndex);
	m_EnvSrvCpu = m_SrvAllocator.Cpu(m_EnvSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = m_EnvMipLevels;
	m_Device->CreateShaderResourceView(m_EnvTexture.Get(), &srv, m_EnvSrvCpu);
}

void DirectXApp::CreateBloomPSOs()
{
	auto make = [&](const char* psEntry, DXGI_FORMAT rtv, ComPtr<ID3D12PipelineState>& out)
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
			desc.pRootSignature = m_PostRootSignature.Get();
			const Shader* vs = m_ShaderLibrary.Load(L"PostProcess.hlsl", "FullScreenVS", "vs_5_0");
			const Shader* ps = m_ShaderLibrary.Load(L"PostProcess.hlsl", psEntry, "ps_5_0");
			if (!vs || !ps) { OutputDebugStringA("Bloom shader load failed\n"); assert(false); return; }
			desc.VS = vs->GetByteCode();
			desc.PS = ps->GetByteCode();
			desc.InputLayout = { nullptr, 0 };
			desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			desc.DepthStencilState.DepthEnable = FALSE;
			desc.DepthStencilState.StencilEnable = FALSE;
			desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
			desc.SampleMask = UINT_MAX;
			desc.SampleDesc.Count = 1;
			desc.NumRenderTargets = 1;
			desc.RTVFormats[0] = rtv;
			m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(out.GetAddressOf()));
		};
	make("BrightPS", DXGI_FORMAT_R16G16B16A16_FLOAT, m_BrightPso);
	make("BlurPS", DXGI_FORMAT_R16G16B16A16_FLOAT, m_BlurPso);
	make("CompositePS", DXGI_FORMAT_R8G8B8A8_UNORM, m_CompositePso);
}

void DirectXApp::CreateDofPSOs()
{
	auto make = [&](const wchar_t* file, const char* psEntry, DXGI_FORMAT rtv,
		ComPtr<ID3D12PipelineState>& out)
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
			desc.pRootSignature = m_PostRootSignature.Get();
			const Shader* vs = m_ShaderLibrary.Load(L"PostProcess.hlsl", "FullScreenVS", "vs_5_0");
			const Shader* ps = m_ShaderLibrary.Load(file, psEntry, "ps_5_0");
			if (!vs || !ps) { OutputDebugStringA("DoF shader load failed\n"); assert(false); return; }
			desc.VS = vs->GetByteCode();
			desc.PS = ps->GetByteCode();
			desc.InputLayout = { nullptr, 0 };
			desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			desc.DepthStencilState.DepthEnable = FALSE;
			desc.DepthStencilState.StencilEnable = FALSE;
			desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
			desc.SampleMask = UINT_MAX;
			desc.SampleDesc.Count = 1;
			desc.NumRenderTargets = 1;
			desc.RTVFormats[0] = rtv;
			m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(out.GetAddressOf()));
		};

	make(L"DepthOfField.hlsl", "DofDownsamplePS", DXGI_FORMAT_R8G8B8A8_UNORM, m_DofDownsamplePso);
	make(L"DepthOfField.hlsl", "DofBlurPS", DXGI_FORMAT_R8G8B8A8_UNORM, m_DofBlurPso);
	make(L"DepthOfField.hlsl", "DofCompositePS", DXGI_FORMAT_R8G8B8A8_UNORM, m_DofCompositePso);
}

void DirectXApp::EnsureDofTargets()
{
	const UINT w = (std::max)(1u, static_cast<UINT>(m_Window_Width));
	const UINT h = (std::max)(1u, static_cast<UINT>(m_Window_Height));

	if (m_DofSceneCopy.IsValid() &&
		m_DofSceneCopy.GetWidth() == w && m_DofSceneCopy.GetHeight() == h)
	{
		return;
	}

	// 反射RTと同じ方針。ビューごとに作り直すと記録済みの描画が
	// 破棄されたリソースを踏むので、ウィンドウ全体ぶんで1回だけ確保する
	if (m_DofSceneCopy.IsValid()) m_DofSceneCopy.Release();
	if (m_DofHalfA.IsValid())     m_DofHalfA.Release();
	if (m_DofHalfB.IsValid())     m_DofHalfB.Release();

	m_DofSceneCopy.Init(w, h);
	m_DofHalfA.Init((std::max)(1u, w / 2), (std::max)(1u, h / 2));
	m_DofHalfB.Init((std::max)(1u, w / 2), (std::max)(1u, h / 2));

	// 合成用のSRVテーブル(3枚が連続している必要がある)
	if (m_DofSrvBase == UINT_MAX)
	{
		m_DofSrvBase = m_SrvAllocator.AllocateRange(3);
		m_DofSrvStart = m_SrvAllocator.Gpu(m_DofSrvBase);
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
	d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	d.Texture2D.MipLevels = 1;

	d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_Device->CreateShaderResourceView(m_DofSceneCopy.GetResource().Get(), &d,
		m_SrvAllocator.Cpu(m_DofSrvBase + 0));
	m_Device->CreateShaderResourceView(m_DofHalfB.GetResource().Get(), &d,
		m_SrvAllocator.Cpu(m_DofSrvBase + 1));

	d.Format = DXGI_FORMAT_R32_FLOAT;	// TYPELESS深度をR32として読む
	m_Device->CreateShaderResourceView(m_Depthbuffer.Get(), &d,
		m_SrvAllocator.Cpu(m_DofSrvBase + 2));
}

void DirectXApp::DepthOfFieldPass(
	RenderTexture& scene,
	UINT w, UINT h,
	const DofCB& cb)
{
	EnsureDofTargets();
	if (!m_DofSceneCopy.IsValid() || m_DofCompositePso == nullptr) return;

	auto* cmd = Cmd();
	const UINT slot = RecordSlot();

	// 合成でシャープ側を読むために、いったんコピーを取る
	// (同じRTを読みながら書くことはできない)
	scene.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_DofSceneCopy.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	{
		PostCB dummy{};
		const auto cbv = m_CBAllocator.Allocate(slot, &dummy, sizeof(dummy));
		PostPass(m_CopyPso.Get(), scene.GetSRV(), cbv, m_DofSceneCopy.GetRTV(), w, h);
	}
	m_DofSceneCopy.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// 深度を読めるようにする
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	const UINT hw = (std::max)(1u, w / 2);
	const UINT hh = (std::max)(1u, h / 2);

	// 縮小 + CoC
	DofCB down = cb;
	down.uv.z = 1.0f / (float)m_DofHalfA.GetWidth();
	down.uv.w = 1.0f / (float)m_DofHalfA.GetHeight();
	{
		const auto cbv = m_CBAllocator.Allocate(slot, &down, sizeof(DofCB));
		m_DofHalfA.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
		PostPass(m_DofDownsamplePso.Get(), m_DofSrvStart, cbv, m_DofHalfA.GetRTV(), hw, hh);
		m_DofHalfA.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// ぼかし。入力は HalfA なので t1 を差し替えたテーブルが要る
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
		d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		d.Texture2D.MipLevels = 1;
		d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_Device->CreateShaderResourceView(m_DofHalfA.GetResource().Get(), &d,
			m_SrvAllocator.Cpu(m_DofSrvBase + 1));

		DofCB blur = down;
		blur.uv.x = 1.0f;	// ぼかしは半解像度どうしなので等倍で引く
		blur.uv.y = 1.0f;
		const auto cbv = m_CBAllocator.Allocate(slot, &blur, sizeof(DofCB));

		m_DofHalfB.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
		PostPass(m_DofBlurPso.Get(), m_DofSrvStart, cbv, m_DofHalfB.GetRTV(), hw, hh);
		m_DofHalfB.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		// t1 をぼかし結果へ戻す
		m_Device->CreateShaderResourceView(m_DofHalfB.GetResource().Get(), &d,
			m_SrvAllocator.Cpu(m_DofSrvBase + 1));
	}

	// 合成。ここで scene を書き戻す
	scene.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	{
		const auto cbv = m_CBAllocator.Allocate(slot, &down, sizeof(DofCB));
		PostPass(m_DofCompositePso.Get(), m_DofSrvStart, cbv, scene.GetRTV(), w, h);
	}

	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void DirectXApp::BuildCompositeSrvTable()
{
	UINT base = m_SrvAllocator.AllocateRange(2);
	m_CompositeSrvStart = m_SrvAllocator.Gpu(base);

	D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
	d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	d.Texture2D.MipLevels = 1;
	d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_Device->CreateShaderResourceView(m_HdrScene.GetResource().Get(), &d, m_SrvAllocator.Cpu(base + 0));
	m_Device->CreateShaderResourceView(m_BloomA.GetResource().Get(), &d, m_SrvAllocator.Cpu(base + 1));
}

void DirectXApp::PostPass(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srvTable, D3D12_GPU_VIRTUAL_ADDRESS cb, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, UINT w, UINT h)
{
	auto* cmd = Cmd();
	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
	D3D12_VIEWPORT vp{ 0,0,(float)w,(float)h,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)w,(LONG)h };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->SetGraphicsRootSignature(m_PostRootSignature.Get());
	cmd->SetPipelineState(pso);
	cmd->SetGraphicsRootDescriptorTable(0, srvTable);
	cmd->SetGraphicsRootConstantBufferView(1, cb);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
}

DirectXApp::~DirectXApp()
{
#ifdef _FRAMEPIPELINE
	{
		std::lock_guard lk(m_GpuExecMutex);
		m_GpuExecExit = true;
	}
	m_GpuExecCv.notify_one();
	if (m_GpuExecThread.joinable())
	{
		m_GpuExecThread.join();
	}
#endif
	WaitForGPUIdle();
	GpuProfiler::Get().Shutdown();
	s_Instance = nullptr;

	if (m_Fence_Event != nullptr)
	{
		CloseHandle(m_Fence_Event);
		m_Fence_Event = nullptr;
	}
}

#ifndef _FRAMEPIPELINE
HRESULT DirectXApp::BeginRender()
{
	const UINT targetIndex = m_FrameIndex;

	const UINT64 fenceToWait = m_FenceValue[targetIndex];
	if (m_Fence->GetCompletedValue() < fenceToWait)
	{
		m_Fence->SetEventOnCompletion(fenceToWait, m_Fence_Event);
		WaitForSingleObject(m_Fence_Event, INFINITE);
	}
	m_DeferredReleases[m_FrameIndex].clear();	// 前フレームの解放予約をクリア

	HRESULT hr = m_CommandAllocator[targetIndex]->Reset();
	if (FAILED(hr)) {
		return hr;
	}

	hr = m_CommandList->Reset(m_CommandAllocator[targetIndex].Get(), nullptr);
	if (FAILED(hr)) {
		return hr;
	}

	GpuProfiler::Get().BeginFrame(m_CommandList.Get(), targetIndex);

	auto dsvhandle = m_DSV_Handle;

	SetResourceBarrier(
		m_CommandList.Get(),
		m_RenderTargets[targetIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

	// グローバル SRV ヒープをフレーム開始時に 1 回だけバインド
	if (m_SrvAllocator.heap)
	{
		ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
		m_CommandList->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	m_CBAllocator.Reset(m_FrameIndex);	// 定数バッファアロケータをリセット

	m_CommandList->SetGraphicsRootSignature(m_rootSignature.Get());

	D3D12_RECT scissorRect = { 0,0,(LONG)m_Window_Width,(LONG)m_Window_Height };
	m_CommandList->RSSetScissorRects(1, &scissorRect);

	D3D12_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width	= (FLOAT)m_Window_Width;
	vp.Height	= (FLOAT)m_Window_Height;
	vp.MinDepth = 0;
	vp.MaxDepth = 1.0f;


	m_CommandList->OMSetRenderTargets(1, &m_RTV_Handle[targetIndex], FALSE, &dsvhandle);

	m_CommandList->RSSetViewports(1, &vp);
	m_CommandList->ClearRenderTargetView(
		m_RTV_Handle[targetIndex],
		ClearColor,
		0,
		nullptr
	);

	m_CommandList->ClearDepthStencilView(
		dsvhandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0, 0, nullptr
	);

	m_CommandList->RSSetViewports(1, &vp);

	return S_OK;
}

HRESULT DirectXApp::EndRender()
{
	const UINT targetIndex = m_FrameIndex;

	SetResourceBarrier(
		m_CommandList.Get(),
		m_RenderTargets[targetIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	GpuProfiler::Get().EndFrame(m_CommandList.Get());

	HRESULT hr = m_CommandList->Close();
	if (FAILED(hr)) {
		return hr;
	}

	ID3D12CommandList* pCommandList = m_CommandList.Get();
	m_CommandQueue->ExecuteCommandLists(1, &pCommandList);

	const UINT64 singnalFenceValue = ++m_NextFenceValue;
	hr = m_CommandQueue->Signal(m_Fence.Get(), singnalFenceValue);
	if (FAILED(hr))
	{
		return hr;
	}

	m_FenceValue[targetIndex] = singnalFenceValue;

	hr = m_SwapChain->Present(1, 0);
	if (FAILED(hr))
	{
		return hr;
	}

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

	return S_OK;
}

#endif
void DirectXApp::EnsureReflectionTarget()
{
	// ウィンドウ全体ぶんで確保しておき、実際に使う範囲はビューポートで絞る。
	// こうしておけばビューを切り替えても作り直しが起きない
	const UINT w = (std::max)(1u, static_cast<UINT>(m_Window_Width));
	const UINT h = (std::max)(1u, static_cast<UINT>(m_Window_Height));

	if (m_ReflectionRT.IsValid() &&
		m_ReflectionRT.GetWidth() == w && m_ReflectionRT.GetHeight() == h)
	{
		return;
	}

	// ここに来るのは初回とウィンドウリサイズのときだけ
	if (m_ReflectionRT.IsValid()) m_ReflectionRT.Release();
	m_ReflectionRT.Init(w, h);
	++m_ReflectionGeneration;

	// 深度も同じ大きさで作り直す(RTVとDSVはサイズを揃える必要がある)
	CD3DX12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32_TYPELESS, w, h,
		1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	CD3DX12_CLEAR_VALUE clear(DXGI_FORMAT_D32_FLOAT, 1.0f, 0);
	CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);

	m_ReflectionDepth.Reset();
	m_Device->CreateCommittedResource(
		&heapProp, D3D12_HEAP_FLAG_NONE, &depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
		IID_PPV_ARGS(m_ReflectionDepth.GetAddressOf()));

	// DSVスロットは作り直しても使い回す
	if (m_ReflectionDsvIndex == UINT_MAX)
	{
		m_DsvAllocator.Allocate(m_ReflectionDsvIndex);
		m_ReflectionDSV = m_DsvAllocator.Cpu(m_ReflectionDsvIndex);
	}

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_Device->CreateDepthStencilView(m_ReflectionDepth.Get(), &dsvDesc, m_ReflectionDSV);
}

void DirectXApp::BeginGeometryPass()
{
	auto* cmd = Cmd();

	// 深度を書き込み状態へ
	m_GBuffer.TransitionToWrite(cmd);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GBuffer::RT_COUNT] =
	{
		m_GBuffer.GetRTV(0),
		m_GBuffer.GetRTV(1),
		m_GBuffer.GetRTV(2),
		m_GBuffer.GetRTV(3)
	};

	constexpr float BlackClear[4] = { 0.0f,0.0f,0.0f,0.0f };

	for (UINT i = 0; i < GBuffer::RT_COUNT; ++i)
	{
		cmd->ClearRenderTargetView(rtvs[i], (i == 3) ? BlackClear : ClearColor, 0, nullptr);
	}
	cmd->ClearDepthStencilView(m_DSV_Handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	cmd->OMSetRenderTargets(GBuffer::RT_COUNT, rtvs, FALSE, &m_DSV_Handle);

	cmd->SetGraphicsRootSignature(m_rootSignature.Get());
}

void DirectXApp::DeferredLightingPass(const RenderContext& ctx)
{
	auto* cmd = Cmd();
	const UINT slot = RecordSlot();

	// --- G-Buffer + 深度を SRV へ ---
	m_GBuffer.TransitionToRead(cmd);
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// --- 出力先: バックバッファ(DSVなし) ---
	const UINT backbuffer = BackbufferIndex();
	cmd->OMSetRenderTargets(1, &m_RTV_Handle[backbuffer], FALSE, nullptr);
	cmd->ClearRenderTargetView(m_RTV_Handle[backbuffer], ClearColor, 0, nullptr);

	cmd->SetGraphicsRootSignature(m_DeferredRootSignature.Get());
	cmd->SetPipelineState(m_DeferredPso.Get());

	// ===== b0 Frame（viewProj/cameraPos）=====
	FrameCB fdata{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		XMStoreFloat4x4(&fdata.viewProj, XMMatrixTranspose(v * p));
		const auto invV = XMMatrixInverse(nullptr, v);
		float4x4 iv; XMStoreFloat4x4(&iv, invV);
		// w には経過秒を入れる(水面の波などが時間を使う)
		fdata.cameraPos = { iv._41, iv._42, iv._43, TIME->GetTotalTime() };
	}
	const auto b0 = m_CBAllocator.Allocate(slot, &fdata, sizeof(fdata));

	// ===== b2 Light =====
	const auto b2 = m_CBAllocator.Allocate(slot, &ctx.lightCb, sizeof(LightCB));

	// ===== b3 invViewProj（深度→ワールド復元用）=====
	DeferredCB dcb{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		const auto inv = XMMatrixInverse(nullptr, v * p);
		// シェーダで mul(clip, invViewProj) と行ベクトル運用なので転置して格納
		XMStoreFloat4x4(&dcb.invViewProj, XMMatrixTranspose(inv));
	}
	const auto b3 = m_CBAllocator.Allocate(slot, &dcb, sizeof(dcb));

	if (b0 == 0 || b2 == 0 || b3 == 0) return; // リング枯渇ガード

	// ルートパラメータ: 0=b0, 1=b2, 2=b3, 3=SRVテーブル
	cmd->SetGraphicsRootConstantBufferView(0, b0);
	cmd->SetGraphicsRootConstantBufferView(1, b2);
	cmd->SetGraphicsRootConstantBufferView(2, b3);
	cmd->SetGraphicsRootDescriptorTable(3, m_GBuffer.GetSrvTableStart());

	cmd->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
	cmd->SetPipelineState(m_VolumetricPso.Get()); // 加算ブレンドPSO（下記で作成）
	cmd->DrawInstanced(3, 1, 0, 0);

	// 深度を書き戻し（スカイ/半透明で使うなら）
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void DirectXApp::Present()
{
	auto* cmd = Cmd();

	const UINT targetIndex = BackbufferIndex();

	auto dsvhandle = m_DsvAllocator.heap->GetCPUDescriptorHandleForHeapStart();
	cmd->OMSetRenderTargets(1, &m_RTV_Handle[targetIndex], FALSE, &dsvhandle);

	D3D12_RECT scissorRect = { 0, 0, (LONG)m_Window_Width, (LONG)m_Window_Height };
	cmd->RSSetScissorRects(1, &scissorRect);

	D3D12_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = static_cast<FLOAT>(m_Window_Width);
	vp.Height = static_cast<FLOAT>(m_Window_Height);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;

	cmd->RSSetViewports(1, &vp);
}

#ifdef _FRAMEPIPELINE

HRESULT DirectXApp::BeginFrameRecord(UINT64 frameNumber)
{
	// 失敗をメインループへ伝搬(Execスレッドで起きたエラー)
	if (FAILED(m_GpuExecResult.load()))
	{
		return m_GpuExecResult.load();
	}

	// このスロットの前回使用者 Exec(N - RTV_NUM) の完了待ち
	// (m_FenceValue[slot] の書き込み完了保証 + コマンドリストのExecute済み保証)
	if (frameNumber >= RTV_NUM)
	{
		std::unique_lock lk(m_GpuExecMutex);
		m_GpuExecDoneCv.wait(lk, [&] {
			return m_GpuExecCompletedCount >= frameNumber - RTV_NUM + 1; });
	}

	m_RecordFrameNumber = frameNumber;
	m_RecordSlot = static_cast<UINT>(frameNumber % RTV_NUM);

	// 再同期点(Execスレッドが更新)から予測
	const UINT64 packed = m_SyncPacked.load();
	const UINT64 syncFrame = packed >> 8;
	const UINT   syncIndex = static_cast<UINT>(packed & 0xFF);
	m_RecordBackbuffer = static_cast<UINT>((syncIndex + (frameNumber - syncFrame)) % RTV_NUM);

	// 子のスロットを最後に使ったフレームのGPU完了を待つ
	const UINT64 fenceToWait = m_FenceValue[m_RecordSlot];
	if (m_Fence->GetCompletedValue() < fenceToWait)
	{
		m_Fence->SetEventOnCompletion(fenceToWait, m_Fence_Event);
		WaitForSingleObject(m_Fence_Event, INFINITE);
	}
	m_DeferredReleases[m_RecordSlot].clear();	// 前フレームの解放予約をクリア

	HRESULT hr = m_CommandAllocator[m_RecordSlot]->Reset();
	if (FAILED(hr)) {
		return hr;
	}

	auto* cmd = m_CommandList[m_RecordSlot].Get();
	hr = cmd->Reset(m_CommandAllocator[m_RecordSlot].Get(), nullptr);
	if (FAILED(hr)) { return hr; }

	// このスロットのフェンス待ちは済んでいるので、ここで前回ぶんの計測結果を回収できる
	GpuProfiler::Get().BeginFrame(cmd, m_RecordSlot);

	SetResourceBarrier(cmd,
		m_RenderTargets[m_RecordBackbuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

	if (m_SrvAllocator.heap)
	{
		ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
		cmd->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	m_CBAllocator.Reset(m_RecordSlot);	// 定数バッファアロケータをリセット

	cmd->SetGraphicsRootSignature(m_rootSignature.Get());

	D3D12_RECT scissorRect = {0,0,(LONG)m_Window_Width,(LONG)m_Window_Height};
	cmd->RSSetScissorRects(1, &scissorRect);

	D3D12_VIEWPORT vp{ 0.0f,0.0f,(FLOAT)m_Window_Width,(FLOAT)m_Window_Height,0.0f,1.0f };

	auto dsvHandle = m_DSV_Handle;
	cmd->OMSetRenderTargets(1, &m_RTV_Handle[m_RecordBackbuffer], FALSE, &dsvHandle);
	cmd->RSSetViewports(1, &vp);
	cmd->ClearRenderTargetView(m_RTV_Handle[m_RecordBackbuffer], ClearColor, 0, nullptr);
	cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	return S_OK;
}

HRESULT DirectXApp::CloseFrameRecord()
{
	auto* cmd = m_CommandList[m_RecordSlot].Get();

	SetResourceBarrier(cmd,
		m_RenderTargets[m_RecordBackbuffer].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	GpuProfiler::Get().EndFrame(cmd);

	return cmd->Close();
}

void DirectXApp::GpuExecThreadMain()
{
	for (;;)
	{
		GPUExecJob job;
		{
			std::unique_lock lk(m_GpuExecMutex);
			m_GpuExecCv.wait(lk, [&] {return m_GpuExecExit || !m_GpuExecQueue.empty(); });
			if (m_GpuExecExit && m_GpuExecQueue.empty())
			{
				return;
			}
			job = m_GpuExecQueue.front();
			m_GpuExecQueue.pop_front();
		}

		const HRESULT hr = ExecuteFrame(job);
		if (FAILED(hr))
		{
			m_GpuExecResult.store(hr);
		}

		{
			std::lock_guard lk(m_GpuExecMutex);
			m_GpuExecCompletedCount = job.frameNumber + 1;
		}
		m_GpuExecDoneCv.notify_all();
	}
}

HRESULT DirectXApp::ExecuteFrame(const GPUExecJob& job)
{
	ID3D12CommandList* lists[] = { m_CommandList[job.slot].Get() };
	m_CommandQueue->ExecuteCommandLists(1, lists);

	const UINT64 signalValue = ++m_NextFenceValue;
	HRESULT hr = m_CommandQueue->Signal(m_Fence.Get(), signalValue);
	if (FAILED(hr)) { return hr; }
	m_FenceValue[job.slot] = signalValue;

	hr = m_SwapChain->Present(1, 0);
	if (FAILED(hr)) { return hr; }

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

	// 予測と実測の照合
	const UINT expected = static_cast<UINT>((job.backbuffer + 1) % RTV_NUM);
	if (m_FrameIndex != expected)
	{
		LOG->LogError("BackBuffer prediction miss: expected=" + std::to_string(expected)
			+ " actual=" + std::to_string(m_FrameIndex)
			+ " frame=" + std::to_string(job.frameNumber));
	}

	// 再同期点を更新: 「フレーム job.frameNumber+1 は index m_FrameIndex に描かれる」
	m_SyncPacked.store(((job.frameNumber + 1) << 8) | m_FrameIndex);

	return S_OK;
}

void DirectXApp::KickExecuteAndPresent()
{
	{
		std::lock_guard lk(m_GpuExecMutex);
		m_GpuExecQueue.push_back({m_RecordFrameNumber, m_RecordSlot, m_RecordBackbuffer});
		m_GpuExecPushedCount = m_RecordFrameNumber + 1;
	}
	m_GpuExecCv.notify_one();
}

void DirectXApp::FlushGpuExec()
{
	std::unique_lock lk(m_GpuExecMutex);
	m_GpuExecDoneCv.wait(lk, [&]{return m_GpuExecCompletedCount >= m_GpuExecPushedCount; });
}

#endif

namespace
{
	/// @brief 等距円筒の (u,v) → 方向ベクトル（SkyBoxShader.hlsl と同じ向き）
	inline float3 EquirectToDirCpu(float u, float v)
	{
		const float phi = (u - 0.5f) * 2.0f * DirectX::XM_PI;
		const float theta = v * DirectX::XM_PI;
		const float st = sinf(theta);
		return { st * cosf(phi), cosf(theta), st * sinf(phi) };
	}

	/// @brief 方向ベクトル → 等距円筒の (u,v)
	inline void DirToEquirectCpu(const float3& d, float& u, float& v)
	{
		u = atan2f(d.z, d.x) / (2.0f * DirectX::XM_PI) + 0.5f;
		v = acosf(std::clamp(d.y, -1.0f, 1.0f)) / DirectX::XM_PI;
	}

	inline const float* EnvTexel(const DirectX::Image& img, int x, int y)
	{
		x = std::clamp(x, 0, static_cast<int>(img.width) - 1);
		y = std::clamp(y, 0, static_cast<int>(img.height) - 1);
		return reinterpret_cast<const float*>(img.pixels + y * img.rowPitch) + x * 4;
	}

	/// @brief 方向でサンプル（最近傍。元が粗いミップなので十分）
	inline float3 SampleEnvDir(const DirectX::Image& img, const float3& d)
	{
		float u, v; DirToEquirectCpu(d, u, v);
		const float* p = EnvTexel(img,
			static_cast<int>(u * img.width), static_cast<int>(v * img.height));
		return { p[0], p[1], p[2] };
	}

	/// @brief Hammersley 列
	inline void HammersleyCpu(uint32_t i, uint32_t n, float& x, float& y)
	{
		uint32_t b = i;
		b = (b << 16) | (b >> 16);
		b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
		b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
		b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
		b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
		x = static_cast<float>(i) / static_cast<float>(n);
		y = static_cast<float>(b) * 2.3283064365386963e-10f;
	}

	/// @brief GGX 重点サンプリング（N=V=R 仮定なので反射方向を返す）
	inline float3 ImportanceGGX(float u1, float u2, const float3& n, float roughness)
	{
		using namespace DirectX;
		const float a = roughness * roughness;
		const float phi = 2.0f * XM_PI * u1;
		const float cosT = sqrtf((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
		const float sinT = sqrtf((std::max)(0.0f, 1.0f - cosT * cosT));

		const XMVECTOR N = XMVector3Normalize(XMLoadFloat3(&n));
		const XMVECTOR up = (fabsf(n.y) < 0.999f) ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
		const XMVECTOR T = XMVector3Normalize(XMVector3Cross(up, N));
		const XMVECTOR B = XMVector3Cross(N, T);

		XMVECTOR h = XMVectorAdd(XMVectorAdd(
			XMVectorScale(T, sinT * cosf(phi)),
			XMVectorScale(B, sinT * sinf(phi))),
			XMVectorScale(N, cosT));
		h = XMVector3Normalize(h);

		const XMVECTOR l = XMVectorSubtract(
			XMVectorScale(h, 2.0f * XMVectorGetX(XMVector3Dot(N, h))), N);
		float3 out; XMStoreFloat3(&out, XMVector3Normalize(l));
		return out;
	}

	/// @brief ミップ列を IBL 用に作り直す。
	///        ミップ n = roughness n/(mips-1) の GGX プレフィルタ。
	///        最も粗い 2 枚はコサイン畳み込み（拡散の放射照度）で上書きする
	void PrefilterEnvironment(DirectX::ScratchImage& chain)
	{
		const size_t mips = chain.GetMetadata().mipLevels;
		if (mips < 3) return;

		// 元データはフィルタ済みの粗めのミップから引く（ファイアフライ抑制）
		const size_t srcMip = (std::min)(static_cast<size_t>(3), mips - 1);
		const DirectX::Image& src = *chain.GetImage(srcMip, 0, 0);

		constexpr uint32_t SAMPLES = 64;

		for (size_t m = 1; m < mips; ++m)
		{
			const DirectX::Image& dst = *chain.GetImage(m, 0, 0);
			const bool diffuse = (m >= mips - 2);           // 最も粗い 2 枚は拡散用
			const float roughness = static_cast<float>(m) / static_cast<float>(mips - 1);

			for (size_t y = 0; y < dst.height; ++y)
			{
				float* row = reinterpret_cast<float*>(dst.pixels + y * dst.rowPitch);
				for (size_t x = 0; x < dst.width; ++x)
				{
					const float3 n = EquirectToDirCpu(
						(x + 0.5f) / dst.width, (y + 0.5f) / dst.height);

					float3 sum{ 0.0f, 0.0f, 0.0f };
					float weight = 0.0f;

					if (diffuse)
					{
						// コサイン畳み込み（総当たり。粗いミップなので軽い）
						for (size_t sy = 0; sy < src.height; ++sy)
						{
							const float theta = (sy + 0.5f) / src.height * DirectX::XM_PI;
							const float solid = sinf(theta);
							for (size_t sx = 0; sx < src.width; ++sx)
							{
								const float3 d = EquirectToDirCpu(
									(sx + 0.5f) / src.width, (sy + 0.5f) / src.height);
								const float ndotl = n.x * d.x + n.y * d.y + n.z * d.z;
								if (ndotl <= 0.0f) continue;
								const float* p = EnvTexel(src, static_cast<int>(sx), static_cast<int>(sy));
								const float w = ndotl * solid;
								sum.x += p[0] * w; sum.y += p[1] * w; sum.z += p[2] * w;
								weight += w;
							}
						}
					}
					else
					{
						for (uint32_t s = 0; s < SAMPLES; ++s)
						{
							float u1, u2; HammersleyCpu(s, SAMPLES, u1, u2);
							const float3 l = ImportanceGGX(u1, u2, n, roughness);
							const float ndotl = n.x * l.x + n.y * l.y + n.z * l.z;
							if (ndotl <= 0.0f) continue;
							const float3 c = SampleEnvDir(src, l);
							sum.x += c.x * ndotl; sum.y += c.y * ndotl; sum.z += c.z * ndotl;
							weight += ndotl;
						}
					}

					const float inv = (weight > 0.0f) ? 1.0f / weight : 0.0f;
					row[x * 4 + 0] = sum.x * inv;
					row[x * 4 + 1] = sum.y * inv;
					row[x * 4 + 2] = sum.z * inv;
					row[x * 4 + 3] = 1.0f;
				}
			}
		}
	}
}

bool DirectXApp::LoadEnvironment(const std::wstring& hdrpath)
{
	std::filesystem::path resolved = hdrpath;
	if (resolved.is_relative() && !std::filesystem::exists(resolved))
	{
		wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
		resolved = std::filesystem::path(exe).parent_path() / hdrpath;
	}

	DirectX::TexMetadata meta{}; DirectX::ScratchImage img{};
	if (FAILED(DirectX::LoadFromHDRFile(resolved.c_str(), &meta, img)))
		return false;

	// 横幅を kMaxEnvWidth に抑える。4096x2048 のままだと
	// R32G32B32A32 + ミップで VRAM を約 170MB 使い、CPU のプレフィルタも重い
	constexpr size_t kMaxEnvWidth = 2048;
	if (meta.width > kMaxEnvWidth)
	{
		const size_t h = (std::max)(size_t(1), meta.height * kMaxEnvWidth / meta.width);
		DirectX::ScratchImage resized;
		if (SUCCEEDED(DirectX::Resize(*img.GetImage(0, 0, 0), kMaxEnvWidth, h,
			DirectX::TEX_FILTER_LINEAR, resized)))
		{
			img = std::move(resized);
			meta = img.GetMetadata();
		}
	}

	// ミップ生成（ラフネス反射のボケ用）
	DirectX::ScratchImage mipped{};
	if (FAILED(DirectX::GenerateMipMaps(*img.GetImage(0, 0, 0),
		DirectX::TEX_FILTER_LINEAR, 0, mipped)))
		mipped = std::move(img);   // 失敗時はミップ無しで続行

	// ミップを IBL 用に作り直す（GGX プレフィルタ＋最粗2枚はコサイン畳み込み）
	PrefilterEnvironment(mipped);

	const DirectX::TexMetadata& mm = mipped.GetMetadata();
	m_EnvMipLevels = static_cast<UINT>(mm.mipLevels);
	{
		const DirectX::Image* top = mipped.GetImage(m_EnvMipLevels - 1, 0, 0);
		if (top && top->pixels)
		{
			// HDRは R32G32B32A32_FLOAT 想定
			const float* p = reinterpret_cast<const float*>(top->pixels);
			m_EnvAmbient = { p[0], p[1], p[2] };
		}
	}

	if (!CreateEnvTextureFromImage(mipped)) return false;

	CreateEnvSrv();

	// ディファードの t5 は起動時に null で張られているので差し替える
	m_GBuffer.SetEnvironment(m_EnvTexture.Get(), m_EnvMipLevels);

	return true;
}

void DirectXApp::WaitForGPUIdle()
{
#ifdef _FRAMEPIPELINE
	FlushGpuExec();
#endif
	if (!m_CommandQueue || !m_Fence || !m_Fence_Event) return;

	ComPtr<ID3D12DeviceRemovedExtendedData> dredData;
	auto device = m_Device.Get();
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dredData))))
	{
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
		D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
		dredData->GetAutoBreadcrumbsOutput(&breadcrumbs);
		dredData->GetPageFaultAllocationOutput(&pageFault);
		LOG->LogError("DRED PageFault VA = " + std::to_string(pageFault.PageFaultVA));
	}

	const UINT64 fenceToWait = ++m_NextFenceValue;
	if(FAILED(m_CommandQueue->Signal(m_Fence.Get(), fenceToWait)))
	{
		return;
	}

	if(m_Fence->GetCompletedValue() < fenceToWait)
	{
		m_Fence->SetEventOnCompletion(fenceToWait, m_Fence_Event);
		WaitForSingleObject(m_Fence_Event, INFINITE);
	}
}

void DirectXApp::CreateMeshShaderPipelineState()
{
	m_MeshPso.Reset();

	if(!m_MeshShaderSupported) {
		return;
	}

	const Shader* mesh = m_ShaderLibrary.Load(L"MS_MeshShader.hlsl", "MeshMain", "ms_6_5");
	const Shader* meshPS = m_ShaderLibrary.Load(L"MS_MeshShader.hlsl", "MeshPS", "ps_6_0");
	if(mesh == nullptr || meshPS == nullptr) {
		assert(false);
		return;
	}

	struct MeshPipelineStateStream
	{
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeRootSingature = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		ID3D12RootSignature* rootSignature = nullptr;

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeMS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
		D3D12_SHADER_BYTECODE ms = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typePS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
		D3D12_SHADER_BYTECODE ps = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeBlend = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
		D3D12_BLEND_DESC blendDesc = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeRasterizer = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
		D3D12_RASTERIZER_DESC rasterizerDesc = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeDepthStencil = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
		D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeRTVFormat = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
		D3D12_RT_FORMAT_ARRAY rtvFormats = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeSampleDesc = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
		DXGI_SAMPLE_DESC sampleDesc = {1, 0};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeSampleMask = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
		UINT sampleMask = UINT_MAX;

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typePrimitiveTopology = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
		D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	};

	MeshPipelineStateStream stream{};
	stream.rootSignature = m_rootSignature.Get();
	stream.ms = mesh->GetByteCode();
	stream.ps = meshPS->GetByteCode();
	stream.blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	stream.rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	stream.depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	stream.rtvFormats.NumRenderTargets = 1;
	stream.rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
	streamDesc.SizeInBytes = sizeof(stream);
	streamDesc.pPipelineStateSubobjectStream = &stream;


	const HRESULT hr = m_Device2 ? m_Device2->CreatePipelineState(
		&streamDesc,
		IID_PPV_ARGS(m_MeshPso.ReleaseAndGetAddressOf())) : E_FAIL;
	if (FAILED(hr))
	{
		m_MeshPso.Reset();
	}
}

void DirectXApp::SetResourceBarrier(ID3D12GraphicsCommandList* CommandList, ID3D12Resource* Resource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
	if(!CommandList || !Resource) {
		return;
	}

	D3D12_RESOURCE_BARRIER descBarrier;
	ZeroMemory(&descBarrier, sizeof(descBarrier));
	descBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	descBarrier.Transition.pResource = Resource;
	descBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	descBarrier.Transition.StateBefore = Before;
	descBarrier.Transition.StateAfter = After;
	CommandList->ResourceBarrier(1, &descBarrier);
}