/*****************************************************************//**
 * \file   RHISmokeTest.cpp
 * \brief  RHI の動作確認（一時的なもの。確認できたらファイルごと消す）
 *         テクスチャ / バッファ / コピー / コマンドリスト / フェンス /
 *         PipelineLayout / PSO / スワップチェイン / 描画 をひと通り通す
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#include "../Defines.hpp"
#include "../Logger.hpp"
#include "IRHIDevice.hpp"
#include "IRHITexture.hpp"
#include "IRHIBuffer.hpp"
#include "IRHICommandList.hpp"
#include "IRHIFence.hpp"
#include "IRHIPipeline.hpp"
#include "IRHISwapChain.hpp"
#include "IRHIDescriptorTable.hpp"
#include "RHIUtil.hpp"

#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	// 頂点の色に定数バッファの色を掛けるだけのシェーダー
	constexpr char TEST_SHADER[] = R"(
cbuffer Params : register(b0)
{
	float4 g_Color;
};

struct VSIn
{
	float2 pos : POSITION;
	float3 col : COLOR;
};

struct VSOut
{
	float4 pos : SV_Position;
	float3 col : COLOR;
};

VSOut VSMain(VSIn i)
{
	VSOut o;
	o.pos = float4(i.pos, 0.0f, 1.0f);
	o.col = i.col;
	return o;
}

float4 PSMain(VSOut i) : SV_Target
{
	return float4(i.col * g_Color.rgb, 1.0f);
}
)";

	// テクスチャ（t0）を画面いっぱいに写すだけのシェーダー。頂点バッファは使わない
	constexpr char TEXTURE_SHADER[] = R"(
Texture2D    g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

struct VSOut
{
	float4 pos : SV_Position;
	float2 uv  : TEXCOORD;
};

// SV_VertexID だけで画面を覆う大きな三角形を作る
VSOut VSMain(uint id : SV_VertexID)
{
	VSOut o;
	o.uv  = float2((id << 1) & 2, id & 2);
	o.pos = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	return o;
}

float4 PSMain(VSOut i) : SV_Target
{
	return g_Texture.Sample(g_Sampler, i.uv);
}
)";

	/// @brief テスト用に D3DCompile で直接コンパイルする（本番は ShaderLibrary を使う）
	ComPtr<ID3DBlob> CompileTestShader(const char* source, const char* entry, const char* target)
	{
		ComPtr<ID3DBlob> code;
		ComPtr<ID3DBlob> error;
		if (FAILED(D3DCompile(source, std::strlen(source), "RHISmokeTest", nullptr, nullptr,
			entry, target, 0, 0, code.GetAddressOf(), error.GetAddressOf())))
		{
			LOG->LogError(std::string("[RHI test] シェーダーのコンパイルに失敗: ")
				+ (error ? static_cast<const char*>(error->GetBufferPointer()) : ""));
			return nullptr;
		}
		return code;
	}

	/// @brief テクスチャ / バッファ / コピー / コマンドリスト / フェンス
	bool TestCopy(IRHIDevice& rhi)
	{
		auto cmd	= rhi.CreateCommandList("RHI Test Copy");
		auto fence	= rhi.CreateFence(0);

		// 深度 + SRV（中では R32_TYPELESS で作られる）
		RHITextureDesc td;
		td.width		= 256;
		td.height		= 256;
		td.format		= ERHIFormat::D32_FLOAT;
		td.usage		= ERHITextureUsage::DepthStencil | ERHITextureUsage::ShaderResource;
		td.debugName	= "RHI Test Depth";
		auto depth = rhi.CreateTexture(td);

		// Upload → GpuOnly → ReadBack と往復させて、中身が戻ってくるか見る
		const uint32_t src[4] = { 1, 2, 3, 4 };
		RHIBufferDesc ud;
		ud.size		= sizeof(src);
		ud.usage	= ERHIBufferUsage::CopySrc;
		ud.memory	= ERHIMemoryType::Upload;
		RHIBufferDesc gd = ud;
		gd.usage	= ERHIBufferUsage::CopySrc | ERHIBufferUsage::CopyDst;
		gd.memory	= ERHIMemoryType::GpuOnly;
		RHIBufferDesc rd = ud;
		rd.usage	= ERHIBufferUsage::CopyDst;
		rd.memory	= ERHIMemoryType::ReadBack;
		auto upload		= rhi.CreateBuffer(ud, src);
		auto gpu		= rhi.CreateBuffer(gd);
		auto readback	= rhi.CreateBuffer(rd);

		if (!cmd || !fence || !depth || !upload || !gpu || !readback) return false;

		cmd->Begin();
		cmd->Transition(depth.get(), ERHIResourceState::Undefined, ERHIResourceState::DepthWrite);
		cmd->Transition(gpu.get(), ERHIResourceState::Undefined, ERHIResourceState::CopyDst);

		// 深度だけのパス（クリアするだけ）
		RHIRenderPassDesc pass;
		pass.depth.texture	= depth.get();
		pass.depth.loadOp	= ERHILoadOp::Clear;
		cmd->BeginRenderPass(pass);
		cmd->EndRenderPass();

		cmd->CopyBuffer(gpu.get(), 0, upload.get(), 0, sizeof(src));
		cmd->Transition(gpu.get(), ERHIResourceState::CopyDst, ERHIResourceState::CopySrc);
		cmd->Transition(depth.get(), ERHIResourceState::DepthWrite,
			ERHIResourceState::DepthRead | ERHIResourceState::ShaderResourcePS);	// 読み取り系は OR できる
		cmd->CopyBuffer(readback.get(), 0, gpu.get(), 0, sizeof(src));
		cmd->End();

		IRHICommandList* lists[] = { cmd.get() };
		rhi.Submit(lists, 1, fence.get(), 1);
		fence->Wait(1);

		const auto* result = static_cast<const uint32_t*>(readback->Map());
		const bool ok = result != nullptr && std::memcmp(result, src, sizeof(src)) == 0;
		readback->Unmap();
		return ok;
	}

	/// @brief テクスチャのアップロード / デスクリプタテーブル / 静的サンプラー / 読み戻し
	/// @note  2x2 の 4 色テクスチャを 2x2 の描き込み先へポイントサンプリングで写し、
	///        読み戻した 4 ピクセルが元と同じになるかを見る
	bool TestTexture(IRHIDevice& rhi)
	{
		// RGBA8 をリトルエンディアンの uint32 で書いている（下位バイトが R）
		const uint32_t texels[4] =
		{
			0xFF0000FF, 0xFF00FF00,	// 赤 緑
			0xFFFF0000, 0xFFFFFFFF,	// 青 白
		};

		RHITextureDesc td;
		td.width		= 2;
		td.height		= 2;
		td.format		= ERHIFormat::R8G8B8A8_UNORM;
		td.usage		= ERHITextureUsage::ShaderResource;
		td.debugName	= "RHI Test Texture";
		auto texture = rhi.CreateTexture(td);

		RHITextureDesc rtd = td;
		rtd.usage		= ERHITextureUsage::RenderTarget;
		rtd.debugName	= "RHI Test Texture RT";
		auto target = rhi.CreateTexture(rtd);

		// ---- レイアウト: t0 のテーブル 1 枚 + s0（ポイント / クランプ）---- //
		RHIPipelineLayoutDesc ld;
		ld.slots			= { { ERHIBindingType::TextureTable, 0, 0, 1, ERHIShaderStage::Pixel } };	// [0] t0
		ld.staticSamplers	= { { 0, 0, ERHIFilter::Point, ERHIAddressMode::Clamp } };
		ld.debugName		= "RHI Test Texture Layout";
		auto layout	= rhi.CreatePipelineLayout(ld);
		auto table	= layout ? rhi.CreateDescriptorTable(layout.get(), 0) : nullptr;

		ComPtr<ID3DBlob> vs = CompileTestShader(TEXTURE_SHADER, "VSMain", "vs_5_0");
		ComPtr<ID3DBlob> ps = CompileTestShader(TEXTURE_SHADER, "PSMain", "ps_5_0");
		if (!texture || !target || !layout || !table || !vs || !ps) return false;

		RHIGraphicsPipelineDesc pd;
		pd.layout							= layout;
		pd.vs								= { vs->GetBufferPointer(), vs->GetBufferSize(), "VSMain" };
		pd.ps								= { ps->GetBufferPointer(), ps->GetBufferSize(), "PSMain" };
		pd.rasterizer.cullMode				= ERHICullMode::None;
		pd.depthStencil.depthTestEnable		= false;
		pd.depthStencil.depthWriteEnable	= false;
		pd.numRenderTargets					= 1;
		pd.rtvFormat[0]						= ERHIFormat::R8G8B8A8_UNORM;
		pd.dsvFormat						= ERHIFormat::Unknown;
		pd.debugName						= "RHI Test Texture PSO";
		auto pso = rhi.CreatePipelineState(pd);

		// 読み戻し先。行の間隔はデバイスに決めてもらう
		RHISubresourceFootprint footprint;
		RHIBufferDesc rbd;
		rbd.size	= rhi.GetCopyableFootprints(target.get(), 0, 1, &footprint);
		rbd.usage	= ERHIBufferUsage::CopyDst;
		rbd.memory	= ERHIMemoryType::ReadBack;
		auto readback = rhi.CreateBuffer(rbd);

		auto cmd	= rhi.CreateCommandList("RHI Test Texture");
		auto fence	= rhi.CreateFence(0);
		if (!pso || !readback || !cmd || !fence) return false;

		cmd->Begin();

		// アップロード
		cmd->Transition(texture.get(), ERHIResourceState::Undefined, ERHIResourceState::CopyDst);
		RHISubresourceData data;
		data.data		= texels;
		data.rowPitch	= sizeof(uint32_t) * 2;
		data.slicePitch	= sizeof(texels);
		RHIBufferRef staging = RHIUploadTexture(rhi, *cmd, texture.get(), 0, 1, &data);
		cmd->Transition(texture.get(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResourcePS);
		table->SetTexture(0, texture);

		// 写す
		cmd->Transition(target.get(), ERHIResourceState::Undefined, ERHIResourceState::RenderTarget);
		RHIRenderPassDesc pass;
		pass.numColors				= 1;
		pass.colors[0].texture		= target.get();
		pass.colors[0].loadOp		= ERHILoadOp::DontCare;	// 全部上書きする
		cmd->BeginRenderPass(pass);
		cmd->SetViewport({ 0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f });
		cmd->SetScissor({ 0, 0, 2, 2 });
		cmd->SetPipelineState(pso.get());
		cmd->SetDescriptorTable(0, table.get());
		cmd->Draw(3);
		cmd->EndRenderPass();

		// 読み戻す
		cmd->Transition(target.get(), ERHIResourceState::RenderTarget, ERHIResourceState::CopySrc);
		cmd->CopyTextureToBuffer(readback.get(), footprint, target.get(), 0);
		cmd->End();

		IRHICommandList* lists[] = { cmd.get() };
		rhi.Submit(lists, 1, fence.get(), 1);
		fence->Wait(1);

		const auto* result = static_cast<const uint8_t*>(readback->Map());
		bool ok = (staging != nullptr) && (result != nullptr);
		for (uint32_t y = 0; ok && y < 2; ++y)
		{
			// バッファ側は行ごとに rowPitch ずつずれて並んでいる
			ok = std::memcmp(result + footprint.offset + static_cast<uint64_t>(footprint.rowPitch) * y,
				&texels[y * 2], sizeof(uint32_t) * 2) == 0;
		}
		readback->Unmap();
		return ok;
	}

	/// @brief PipelineLayout / PSO / スワップチェイン / 描画
	bool TestDraw(IRHIDevice& rhi, HWND hwnd, uint32_t width, uint32_t height)
	{
		RHISwapChainDesc sd;
		sd.windowHandle	= hwnd;
		sd.width		= width;
		sd.height		= height;
		sd.bufferCount	= RTV_NUM;
		sd.format		= ERHIFormat::R8G8B8A8_UNORM;
		sd.vSync		= true;
		auto swapChain = rhi.CreateSwapChain(sd);
		if (!swapChain) return false;

		// ---- レイアウト: b0 だけ ---- //
		RHIPipelineLayoutDesc ld;
		ld.slots		= { { ERHIBindingType::ConstantBuffer, 0, 0, 1, ERHIShaderStage::Pixel } };	// [0] b0
		ld.debugName	= "RHI Test Layout";
		auto layout = rhi.CreatePipelineLayout(ld);

		// ---- PSO ---- //
		ComPtr<ID3DBlob> vs = CompileTestShader(TEST_SHADER, "VSMain", "vs_5_0");
		ComPtr<ID3DBlob> ps = CompileTestShader(TEST_SHADER, "PSMain", "ps_5_0");
		if (!layout || !vs || !ps) return false;

		RHIGraphicsPipelineDesc pd;
		pd.layout	= layout;
		pd.vs		= { vs->GetBufferPointer(), vs->GetBufferSize(), "VSMain" };
		pd.ps		= { ps->GetBufferPointer(), ps->GetBufferSize(), "PSMain" };
		pd.vertexAttributes =
		{
			{ "POSITION", 0, ERHIFormat::R32G32_FLOAT,    0, 0 },
			{ "COLOR",    0, ERHIFormat::R32G32B32_FLOAT, 0, 8 },
		};
		pd.vertexBuffers					= { { sizeof(float) * 5, false } };
		pd.rasterizer.cullMode				= ERHICullMode::None;
		pd.depthStencil.depthTestEnable		= false;
		pd.depthStencil.depthWriteEnable	= false;
		pd.numRenderTargets					= 1;
		pd.rtvFormat[0]						= swapChain->GetFormat();
		pd.dsvFormat						= ERHIFormat::Unknown;
		pd.debugName						= "RHI Test PSO";
		auto pso = rhi.CreatePipelineState(pd);
		if (!pso) return false;

		// ---- 三角形と定数バッファ ---- //
		const float vertices[] =
		{
			//  x      y      r     g     b
			 0.0f,  0.5f,  1.0f, 0.0f, 0.0f,
			 0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
			-0.5f, -0.5f,  0.0f, 0.0f, 1.0f,
		};
		RHIBufferDesc vbd;
		vbd.size		= sizeof(vertices);
		vbd.usage		= ERHIBufferUsage::VertexBuffer;
		vbd.memory		= ERHIMemoryType::Upload;	// テストなので Upload から直接読ませる
		vbd.debugName	= "RHI Test VB";
		auto vb = rhi.CreateBuffer(vbd, vertices);

		RHIBufferDesc cbd;
		cbd.size		= sizeof(float) * 4;	// 256 に切り上がる
		cbd.usage		= ERHIBufferUsage::ConstantBuffer;
		cbd.memory		= ERHIMemoryType::Upload;
		cbd.debugName	= "RHI Test CB";
		auto cb = rhi.CreateBuffer(cbd);

		auto cmd	= rhi.CreateCommandList("RHI Test Draw");
		auto fence	= rhi.CreateFence(0);
		if (!vb || !cb || !cmd || !fence) return false;

		// ---- 2 秒ほど回す（色を変えて、定数バッファの更新も確かめる）---- //
		constexpr uint32_t FRAMES = 120;
		uint64_t fenceValue = 0;
		for (uint32_t frame = 0; frame < FRAMES && IsWindow(hwnd); ++frame)
		{
			// テスト用ウィンドウのメッセージだけ処理する（本体のウィンドウはまだ初期化中なので触らない）
			MSG msg;
			while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			// 毎フレーム GPU の完了を待っているので、定数バッファもリストもそのまま使い回せる
			const float t = static_cast<float>(frame) / FRAMES;
			const float color[4] = { 1.0f, 1.0f - t, 1.0f, 1.0f };
			std::memcpy(cb->Map(), color, sizeof(color));

			cmd->Begin();

			const uint32_t index = swapChain->AcquireNextImage();
			IRHITexture* backBuffer = swapChain->GetBackBuffer(index);
			cmd->Transition(backBuffer, ERHIResourceState::Undefined, ERHIResourceState::RenderTarget);

			RHIRenderPassDesc pass;
			pass.numColors					= 1;
			pass.colors[0].texture			= backBuffer;
			pass.colors[0].loadOp			= ERHILoadOp::Clear;
			pass.colors[0].clearColor[0]	= 0.1f;
			pass.colors[0].clearColor[1]	= 0.1f;
			pass.colors[0].clearColor[2]	= 0.2f;
			cmd->BeginRenderPass(pass);
			cmd->SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
			cmd->SetScissor({ 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) });
			cmd->SetPipelineState(pso.get());
			cmd->SetConstantBuffer(0, cb.get());
			cmd->SetVertexBuffer(0, vb.get());
			cmd->Draw(3);
			cmd->EndRenderPass();

			cmd->Transition(backBuffer, ERHIResourceState::RenderTarget, ERHIResourceState::Present);
			cmd->End();

			IRHICommandList* lists[] = { cmd.get() };
			rhi.Submit(lists, 1, fence.get(), ++fenceValue);
			if (swapChain->Present() != ERHIPresentResult::Ok) return false;
			fence->Wait(fenceValue);
		}
		return true;
	}
}

bool RunRHISmokeTest()
{
	// ---- テスト用ウィンドウ（スワップチェインは 1 ウィンドウに 1 つしか作れないので本体とは別）---- //
	constexpr uint32_t WIDTH	= 640;
	constexpr uint32_t HEIGHT	= 360;
	const HINSTANCE instance = GetModuleHandleW(nullptr);

	WNDCLASSEXW wc{};
	wc.cbSize			= sizeof(wc);
	wc.lpfnWndProc		= DefWindowProcW;
	wc.hInstance		= instance;
	wc.hCursor			= LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName	= L"RHISmokeTest";
	RegisterClassExW(&wc);

	RECT rc{ 0, 0, static_cast<LONG>(WIDTH), static_cast<LONG>(HEIGHT) };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"RHI Smoke Test",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, instance, nullptr);

	bool copyOK		= false;
	bool textureOK	= false;
	bool drawOK		= false;
	{
		RHIDeviceDesc dd;
		dd.frameInFlight	= RTV_NUM;
		dd.enableDebugLayer	= false;	// DirectXApp が先に有効にしている。ここで有効にすると既存デバイスが Removed になる
		auto rhi = CreateRHIDevice(dd);
		if (rhi)
		{
			copyOK		= TestCopy(*rhi);
			textureOK	= TestTexture(*rhi);
			drawOK		= (hwnd != nullptr) && TestDraw(*rhi, hwnd, WIDTH, HEIGHT);
		}
		// rhi はここで壊れる。デストラクタが GPU を待ってから遅延解放を全部済ませる
	}

	if (hwnd != nullptr) DestroyWindow(hwnd);
	UnregisterClassW(wc.lpszClassName, instance);

	LOG->LogInfo(std::string("[RHI test] copy ") + (copyOK ? "OK" : "NG")
		+ " / texture " + (textureOK ? "OK" : "NG")
		+ " / draw " + (drawOK ? "OK" : "NG"));
	return copyOK && textureOK && drawOK;
}
