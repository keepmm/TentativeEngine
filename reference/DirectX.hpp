#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
// DirectXTex はヘッダでは引数の型として名前を使うだけなので前方宣言で済ませる。
// ここで include するとスクリプト(Scripts.dll)まで DirectXTex の include パスが必要になり、
// MonoBehavior.hpp 経由でビルドが通らなくなる
namespace DirectX { class ScratchImage; }

#include <atomic>
#include <thread>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "RenderContext.hpp"
#include "ShaderLibrary.hpp"
#include "PipelineStateCache.hpp"

#include "Defines.hpp"
#include "ShadowMap.hpp"
#include "GBuffer.hpp"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

#define _FRAMEPIPELINE

struct DescriptorAllocator
{
	ComPtr<ID3D12DescriptorHeap> heap;
	UINT decSize = 0;	// デスクリプタヒープ内のサイズ
	UINT capacity = 0;	// デスクリプタヒープの容量
	std::vector<UINT> freeList;	// 空いているスロットのリスト
	UINT next = 0;

	void Init(
		_In_ ID3D12Device* device,
		_In_ D3D12_DESCRIPTOR_HEAP_TYPE type,
		_In_ UINT count,
		_In_ bool shaderVisible)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type			 = type;
		desc.NumDescriptors = count;
		desc.Flags			= shaderVisible 
			? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
			: D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.ReleaseAndGetAddressOf()));
		decSize		= device->GetDescriptorHandleIncrementSize(type);
		capacity	= count;
		next		= 0;
		freeList.clear();
	}

	/// @brief スロットの確保
	/// @param outIndex 確保するIndex
	/// @return 確保できた場合True 
	bool Allocate(UINT& outIndex)
	{
		if (!freeList.empty())
		{
			outIndex = freeList.back();
			freeList.pop_back();
			return true;
		}

		// 次のスロットがキャパシティを超えた場合
		// 確保失敗
		if (next >= capacity)
		{
			return false;
		}

		// 線形確保
		outIndex = next++;
		return true;
	}

	UINT AllocateRange(UINT count)
	{
		if (next + count > capacity)
		{
			return UINT_MAX; // 確保失敗
		}
		UINT startIndex = next;
		next += count;
		return startIndex;
	}

	void Free(UINT index)
	{
		if (index < capacity)
		{
			freeList.push_back(index);
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT i)const
	{
		auto h = heap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += static_cast<SIZE_T>(i) * decSize;
		return h;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT i)const
	{
		auto h = heap->GetGPUDescriptorHandleForHeapStart();
		h.ptr += static_cast<SIZE_T>(i) * decSize;
		return h;
	}
};

struct RegisteredPass
{
	ShaderPassDef def;
	ComPtr<ID3D12PipelineState> pso;
};

#include "ConstantBufferAllocator.hpp"

#define APP DirectXApp::GetCurrent()

class DirectXApp
{
public:
	/// @brief グローバル SRV ヒープのスロット数
	static constexpr UINT SRV_HEAP_SIZE = 256;

	/// @brief ボリュームライトを何分の1の解像度でレイマーチするか。
	///        ビームは滑らかなので落としても粗が出にくく、コストは除数の2乗で効く
	static constexpr UINT VOLUMETRIC_DIV = 4;

	/// @brief グローバル SRV ヒープを持つ唯一の DirectXApp インスタンスを返す
	static DirectXApp* GetCurrent() noexcept { return s_Instance; }

	static constexpr size_t VERTEX_SHADER_COUNT = static_cast<size_t>(E_VERTEX_SHADER::COUNT);
	static constexpr size_t PIXEL_SHADER_COUNT = static_cast<size_t>(E_PIXEL_SHADER::COUNT);

	using PipelineStateTable = std::array<std::array<ComPtr<ID3D12PipelineState>, PIXEL_SHADER_COUNT>, VERTEX_SHADER_COUNT>;

public:

	DirectXApp(HWND hWnd, int Window_Width, int Window_Height);

	~DirectXApp();

	void BeginGeometryPass();
	void DeferredLightingPass(const RenderContext& ctx);
#ifdef _FRAMEPIPELINE
	HRESULT BeginFrameRecord(UINT64 frameNumber);
	HRESULT CloseFrameRecord();
	void KickExecuteAndPresent();
	HRESULT GetGpuExecResult() const noexcept { return m_GpuExecResult.load(); }
	void FlushGpuExec();
#else
	HRESULT BeginRender();
	HRESULT EndRender();
#endif
	void Present();

	bool ReloadShader();

	bool CreateShadeFromSource(
		_In_ const std::string& name,
		_In_ const std::string& hlslCode,
		_In_ std::string& psEntry,
		_Out_ std::string& outError,
		_In_ bool alphaBlend = false
	);

	void DeferredLightingPass(
		_In_ const RenderContext& ctx,
		D3D12_CPU_DESCRIPTOR_HANDLE targetRtv,
		_In_ const D3D12_VIEWPORT& viewport,
		_In_ const D3D12_RECT& sc
	);

	void PostProcessCopy(
		RenderTexture& src,
		D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
		const D3D12_VIEWPORT& vp, const D3D12_RECT& sc
	);

	void PostProcessBloom(D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
		const D3D12_VIEWPORT& vp, const D3D12_RECT& sc);

	ID3D12PipelineState* RegisterShaderPass(
		_In_ const std::string& name,
		_In_ const ShaderPassDef& def
	);
	ID3D12PipelineState* GetPipelineStateByName(
		_In_ const std::string& name
	)const;
	std::vector<std::string> GetShaderNames() const;
	/// @brief その名前のシェーダーパスが登録済みか(HDR双子の有無判定に使う)
	bool HasShaderPass(_In_ const std::string& name) const;

	// ---------------------------------------------------//
	//						Getter						  //
	// ---------------------------------------------------//
	inline UINT GetFrameSlot() const noexcept
	{
#ifdef _FRAMEPIPELINE
		return m_RecordSlot;
#else
		return m_FrameIndex;
#endif
	}
	inline ComPtr<ID3D12Device> GetDevice() const noexcept { return m_Device; }
	inline ComPtr<ID3D12GraphicsCommandList> GetCommandList() const noexcept 
	{ 
#ifdef _FRAMEPIPELINE
		return m_CommandList[m_RecordSlot];
#else
		return m_CommandList; 
#endif
	}
	inline ID3D12GraphicsCommandList6* GetCommandList6() const noexcept
	{
#ifdef _FRAMEPIPELINE
		return m_CommandList6[m_RecordSlot].Get();
#else
		return m_CommandList6.Get();
#endif
	}
	inline ComPtr<ID3D12RootSignature> GetRootSignature() const noexcept { return m_rootSignature; }
	inline ComPtr<ID3D12PipelineState> GetPipelineStateWireFrame() const noexcept { return m_pipelineStateWireFrame; }
	inline ComPtr<ID3D12PipelineState> GetLinePso() const noexcept { return m_LinePso; }
	inline ComPtr<ID3D12PipelineState> GetIconPso() const noexcept { return m_IconPso; }
	inline ID3D12PipelineState* GetMeshPso() const noexcept { return m_MeshPso.Get(); }
	inline ID3D12PipelineState* GetUIPso() const noexcept { return m_UIPso.Get(); }
	inline ID3D12PipelineState* GetSkyPso() const noexcept { return m_SkyPso.Get(); }
	/// @brief 直近のディファードライティングに渡った灯数(プロファイル表示用)
	inline UINT GetLastLightCount() const noexcept { return m_LastLightCount; }
	/// @brief そのうちボリュームライトとして評価された灯数
	inline UINT GetLastVolumetricCount() const noexcept { return m_LastVolumetricCount; }
	inline ID3D12PipelineState* GetSkyHdrPso() const noexcept { return m_SkyHdrPso.Get(); }
	inline ID3D12PipelineState* GetShadowPso() const noexcept { return m_ShadowPso.Get(); }
	inline ID3D12PipelineState* GetBeamPso() const noexcept { return m_BeamPso.Get(); }
	inline ID3D12PipelineState* GetGBufferPso() const noexcept { return m_GBufferPso.Get(); }
	inline ID3D12PipelineState* GetDeferredPso() const noexcept { return m_DeferredPso.Get(); }
	inline ID3D12PipelineState* GetPostPso() const noexcept { return m_PostPso.Get(); }
	inline ID3D12PipelineState* GetBeamHdrPso() const noexcept { return m_BeamHdrPso.Get(); }
	inline ID3D12PipelineState* GetCopyPso() const noexcept { return m_CopyPso.Get(); }
	inline ID3D12PipelineState* GetVolumetricPso() const noexcept { return m_VolumetricPso.Get(); }
	inline ID3D12PipelineState* GetVolumetricAddPso() const noexcept {return m_VolumetricAddPso.Get(); }
	inline ID3D12PipelineState* GetFireworkPso() const noexcept { return m_FireworkPso.Get(); }
	inline ComPtr<ID3D12PipelineState> GetLineDepthPso() const noexcept { return m_LineDepthPso; }
	inline ComPtr<ID3D12PipelineState> GetLineHdrDepthPso() const noexcept { return m_LineHdrDepthPso; }

	template<class T>
	void DederredRelease(T&& r)
	{
		if (r)
		{
			m_DeferredReleases[RecordSlot()].push_back(std::move(r));
		}
	}

	inline bool IsMeshShaderSupported() const noexcept { return m_MeshShaderSupported; }
	inline ComPtr<ID3D12CommandQueue> GetCommandQueue() const noexcept { return m_CommandQueue; }

	/// @brief 被写界深度をかける。シーンを描き終えたRTをその場で置き換える
	/// @param scene シーンを描いたRT。読み書き両方に使うので状態遷移はこの中で行う
	/// @param w,h   scene のうち実際に描いている範囲
	/// @param cb    焦点などのパラメータ
	void DepthOfFieldPass(
		_In_ RenderTexture& scene,
		_In_ UINT w, _In_ UINT h,
		_In_ const DofCB& cb);

	/// @brief 平面反射のレンダーターゲットを用意する
	/// @note ウィンドウサイズで一度だけ確保する。ビューごとに作り直すと、
	///       同じフレームに記録済みの描画が破棄されたリソースを参照して
	///       GPUがページフォルトする。解像度の調整はサブ矩形(ビューポート)で行う
	void EnsureReflectionTarget();

	inline RenderTexture& GetReflectionRT() noexcept { return m_ReflectionRT; }
	inline float GetReflectionScale() const noexcept { return m_ReflectionScale; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetReflectionDSV() const noexcept { return m_ReflectionDSV; }

	/// @brief 反射RTを作り直すたびに増える。マテリアル側のSRV張り直しの判定に使う
	inline UINT GetReflectionGeneration() const noexcept { return m_ReflectionGeneration; }
	inline void  SetReflectionScale(float s) noexcept { m_ReflectionScale = s; }
	inline UINT GetFrameIndex() const noexcept { return m_FrameIndex; }
	inline ConstantBufferAllocator& GetConstantBufferAllocator() noexcept { return m_CBAllocator; }
	inline RenderTexture& GetHdrScene() { return m_HdrScene; }

	bool IsShaderAlphaBlend(const std::string& name) const
	{
		auto it = m_ShaderRegistry.find(name);
		return it != m_ShaderRegistry.end() && it->second.def.alphaBlend;
	}

	/// @brief グローバル SRV ヒープを返す（SetDescriptorHeaps 用）
	ID3D12DescriptorHeap* GetSrvHeap() const noexcept { return m_SrvAllocator.heap.Get(); }
	inline const PipelineStateTable& GetPipelineStates() const noexcept { return m_PipelineStates; }
	inline ComPtr<ID3D12PipelineState> GetPipelineState() const noexcept
	{
		return m_PipelineStates[static_cast<size_t>(E_VERTEX_SHADER::BASIC)][static_cast<size_t>(E_PIXEL_SHADER::BASIC)];
	}
	inline ComPtr<ID3D12PipelineState> GetPipelineState(E_VERTEX_SHADER vs, E_PIXEL_SHADER ps) const noexcept
	{
		return m_PipelineStates[static_cast<size_t>(vs)][static_cast<size_t>(ps)];
	}

	inline DescriptorAllocator& GetSrvAllocator() noexcept { return m_SrvAllocator; }
	inline DescriptorAllocator& GetRtvAllocator() noexcept { return m_RtvAllocator; }
	inline DescriptorAllocator& GetDsvAllocator() noexcept { return m_DsvAllocator; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const noexcept { return m_DSV_Handle; }
	// 記録中のコマンドリストが対象にしているバックバッファを返す
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const noexcept { return m_RTV_Handle[BackbufferIndex()]; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetShadowSrvCpu() const noexcept { return m_ShadowSrvCpu; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetEnvSrvCpu() const noexcept { return m_EnvSrvCpu; }
	bool LoadEnvironment(const std::wstring& hdrpath);
	ID3D12Resource* GetEnvTexture() const { return m_EnvTexture.Get(); }
	UINT GetEnvMipLevels() const { return m_EnvMipLevels; }
	const float3& GetEnvAmbient() const { return m_EnvAmbient; }
	bool HasEnvironment() const { return m_EnvTexture != nullptr; }
	UINT GetEnvGeneration() const noexcept { return m_EnvGeneration; }
	inline ShadowMap& GetShadowMap() noexcept { return m_ShadowMap; }

	void WaitForGPUIdle();
	void DeferRelease(ComPtr<ID3D12Resource>&& r)
	{
		if (r) m_DeferredReleases[RecordSlot()].push_back(std::move(r));
	}

private:
	static DirectXApp* s_Instance;

	HWND m_Window_hWnd;
	int  m_Window_Width;
	int  m_Window_Height;
	HANDLE m_Fence_Event;
	std::atomic<UINT64> m_NextFenceValue = 0;
	ComPtr<ID3D12Device> m_Device;
	ComPtr<ID3D12Device2> m_Device2;
	ComPtr<ID3D12CommandQueue> m_CommandQueue;
	ComPtr<ID3D12CommandAllocator>		m_CommandAllocator[RTV_NUM];
#ifdef _FRAMEPIPELINE
	ComPtr<ID3D12GraphicsCommandList>	m_CommandList[RTV_NUM];

	UINT64  m_RecordFrameNumber = 0;
	UINT	m_RecordSlot = 0;
	UINT	m_RecordBackbuffer = 0;
	UINT	m_SyncBackBuffer = 0;
	UINT64  m_SyncFrameNumber = 0;   

	std::atomic<UINT64> m_SyncPacked{ 0 };

	/// @brief GpuExecスレッド
	struct GPUExecJob
	{
		UINT64 frameNumber;
		UINT slot;
		UINT backbuffer;
	};
	std::thread				m_GpuExecThread;
	std::mutex				m_GpuExecMutex;
	std::condition_variable m_GpuExecCv;
	std::condition_variable m_GpuExecDoneCv;
	std::deque<GPUExecJob>  m_GpuExecQueue;
	UINT64					m_GpuExecCompletedCount = 0;
	UINT64					m_GpuExecPushedCount = 0;
	bool					m_GpuExecExit = false;
	std::atomic<HRESULT>	m_GpuExecResult{ S_OK };

	void GpuExecThreadMain();
	HRESULT ExecuteFrame(_In_ const GPUExecJob& job);
#else
	ComPtr<ID3D12GraphicsCommandList>	m_CommandList;
#endif
	ComPtr<IDXGISwapChain3> m_SwapChain;
	ComPtr<ID3D12Fence> m_Fence;
	ComPtr<IDXGIFactory3> m_Factory;


	DescriptorAllocator m_SrvAllocator;
	DescriptorAllocator m_RtvAllocator;
	DescriptorAllocator m_DsvAllocator;

	ConstantBufferAllocator m_CBAllocator;

	D3D12_CPU_DESCRIPTOR_HANDLE m_DSV_Handle;

	ComPtr<ID3D12Resource> m_RenderTargets[RTV_NUM];
	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV_Handle[RTV_NUM];
	std::atomic<UINT> m_FrameIndex = 0;
	UINT64 m_FenceValue[RTV_NUM] = {};


	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12RootSignature> m_DeferredRootSignature;
	ComPtr<ID3D12RootSignature> m_PostRootSignature;

	PipelineStateTable m_PipelineStates{};
	ComPtr<ID3D12PipelineState> m_pipelineStateWireFrame;
	ComPtr<ID3D12PipelineState> m_LinePso;
	ComPtr<ID3D12PipelineState> m_IconPso;
	ComPtr<ID3D12PipelineState> m_UIPso;
	UINT m_LastLightCount = 0;
	UINT m_LastVolumetricCount = 0;
	ComPtr<ID3D12PipelineState> m_SkyPso;
	ComPtr<ID3D12PipelineState> m_SkyHdrPso;
	ComPtr<ID3D12PipelineState> m_ShadowPso;
	ComPtr<ID3D12PipelineState> m_BeamPso;
	ComPtr<ID3D12PipelineState> m_BeamHdrPso;
	ComPtr<ID3D12PipelineState> m_GBufferPso;
	ComPtr<ID3D12PipelineState> m_DeferredPso;
	ComPtr<ID3D12PipelineState> m_PostPso;
	ComPtr<ID3D12PipelineState> m_CopyPso;
	ComPtr<ID3D12PipelineState> m_VolumetricPso;
	ComPtr<ID3D12PipelineState> m_VolumetricAddPso;
	ComPtr<ID3D12PipelineState> m_LineDepthPso;
	ComPtr<ID3D12PipelineState> m_LineHdrDepthPso;

	ComPtr<ID3D12PipelineState> m_FireworkPso;

#ifdef _FRAMEPIPELINE

	ComPtr<ID3D12GraphicsCommandList6> m_CommandList6[RTV_NUM];
#else
	ComPtr<ID3D12GraphicsCommandList6> m_CommandList6;
#endif
	ComPtr<ID3D12PipelineState> m_MeshPso;
	bool m_MeshShaderSupported = false;

	void CreateMeshShaderPipelineState();

	ShaderLibrary m_ShaderLibrary;
	PipelineStateCache m_PsoCache;

	ComPtr<ID3D12Resource> m_Depthbuffer;

	void SetResourceBarrier(
		ID3D12GraphicsCommandList* CommandList,
		ID3D12Resource* Resource,
		D3D12_RESOURCE_STATES Before,
		D3D12_RESOURCE_STATES After);

	void CreateRootSignature();
	void CreateDeferredRootSignature();
	void CreatePostRootSignature();
	void CreatePipelineStateObject();

	std::unordered_map<std::string, RegisteredPass> m_ShaderRegistry;

	void RegisterBuiltinShaders();

	D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeBasePsoDesc()const;

	// Environment Map
	ComPtr<ID3D12Resource> m_EnvTexture;
	UINT m_EnvMipLevels = 1;
	UINT m_EnvGeneration = 0;	// 差し替えを検知させる

	/// @brief ScratchImage を GPU テクスチャにして m_EnvTexture に入れる
	bool CreateEnvTextureFromImage(const DirectX::ScratchImage& image);

	float3 m_EnvAmbient = { 0.1f,0.1f,0.1f };
	ShadowMap m_ShadowMap{};
	std::vector<ComPtr<ID3D12Resource>> m_DeferredReleases[RTV_NUM];

	void CreateGbufferPSO();
	void CreateDeferredPSO();
	void CreatePostPSO();
	void CreateCopyPSO();
	void CreateVolumetricPSO();
	void CreateShadowSrv();
	void CreateEnvSrv();

	D3D12_CPU_DESCRIPTOR_HANDLE m_DepthSrvHandleCpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE m_DepthSrvHandleGpu{};

	GBuffer m_GBuffer{};
	UINT m_ShadowSrvIndex = UINT_MAX;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowSrvCpu{};
	UINT m_EnvSrvIndex = UINT_MAX;
	D3D12_CPU_DESCRIPTOR_HANDLE m_EnvSrvCpu{};

	RenderTexture m_HdrScene;
	RenderTexture m_BloomA;
	RenderTexture m_BloomB;
	RenderTexture m_VolumetricHalf;

	// 被写界深度。どちらもウィンドウ全体ぶんで確保し、使う範囲はビューポートで絞る
	RenderTexture m_DofSceneCopy;	// 合成前のシーン(等倍)
	RenderTexture m_DofHalfA;		// 縮小 + CoC
	RenderTexture m_DofHalfB;		// ぼかし済み
	ComPtr<ID3D12PipelineState> m_DofDownsamplePso;
	ComPtr<ID3D12PipelineState> m_DofBlurPso;
	ComPtr<ID3D12PipelineState> m_DofCompositePso;
	D3D12_GPU_DESCRIPTOR_HANDLE m_DofSrvStart{};	// t0:等倍 t1:ぼかし t2:深度
	UINT m_DofSrvBase = UINT_MAX;

	void CreateDofPSOs();
	void EnsureDofTargets();

	// 平面反射(床に映すぶん)。シーンRTの m_ReflectionScale 倍で持つ
	RenderTexture m_ReflectionRT;
	float m_ReflectionScale = 0.5f;

	// 反射RTはシーンRTより小さいので、深度も専用に持つ
	ComPtr<ID3D12Resource>      m_ReflectionDepth;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ReflectionDSV{};
	UINT                        m_ReflectionDsvIndex = UINT_MAX;
	UINT                        m_ReflectionGeneration = 0;
	ComPtr<ID3D12PipelineState> m_BrightPso;
	ComPtr<ID3D12PipelineState> m_BlurPso;
	ComPtr<ID3D12PipelineState> m_CompositePso;
	D3D12_GPU_DESCRIPTOR_HANDLE m_CompositeSrvStart{};

	void CreateBloomPSOs();
	void BuildCompositeSrvTable();
	void PostPass(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srvTable,
		D3D12_GPU_VIRTUAL_ADDRESS cb, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
		UINT w, UINT h);

	/// @brief 現在記録対象のコマンドリスト
	ID3D12GraphicsCommandList* Cmd() const noexcept
	{
#ifdef _FRAMEPIPELINE
		return m_CommandList[m_RecordSlot].Get();
#else
		return m_CommandList.Get();
#endif
	}

	/// @brief CBアロケータ/DeferredReleaseのスロットキー
	UINT RecordSlot() const noexcept
	{
#ifdef _FRAMEPIPELINE
		return m_RecordSlot;
#else
		return m_FrameIndex;
#endif
	}

	UINT BackbufferIndex() const noexcept
	{
#ifdef _FRAMEPIPELINE
		return m_RecordBackbuffer;
#else
		return m_FrameIndex;
#endif
	}
};
