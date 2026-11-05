/*****************************************************************//**
 * \file   ImGuiRenderer.cpp
 * \brief  ImGuiの描画データをRHIで描く
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#include "UI/ImGuiRenderer.hpp"

#include "Core/Log.hpp"
#include "Renderer/ShaderCompiler.hpp"
#include "RHI/IRHIBuffer.hpp"
#include "RHI/IRHICommandList.hpp"
#include "RHI/IRHIDescriptorTable.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRHIFence.hpp"
#include "RHI/IRHIPipeline.hpp"
#include "RHI/IRHITexture.hpp"
#include "RHI/RHIUtil.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace
{
	// シェーダーの cbuffer ImGuiConstants と同じ並び。画面の座標（ピクセル）→ NDC への変換
	struct ImGuiConstants
	{
		float scale[2];
		float translate[2];
	};
	static_assert(sizeof(ImGuiConstants) % 16 == 0, "cbuffer の大きさは 16 バイトの倍数にする");
	static_assert(std::is_trivially_copyable_v<ImGuiConstants>, "GPU へは memcpy で送るので、ただのデータにする");

	// パイプラインレイアウトのスロット番号。D3D11 では定数バッファのスロット番号 = レジスタ番号なので揃える
	constexpr std::uint32_t kConstantsSlot = 0;	// b0
	constexpr std::uint32_t kTextureSlot = 1;	// t0

	constexpr std::uint64_t kConstantBufferSize = 256;	// D3D12 の決まり
	constexpr std::size_t kInitialVertexCapacity = 5000;
	constexpr std::size_t kInitialIndexCapacity = 10000;

	// ImDrawIdx を 32 ビットに変えたとき（imconfig.h）にも合うよう、ここで形式を決める
	constexpr ERHIFormat kIndexFormat = sizeof(ImDrawIdx) == 2 ? ERHIFormat::R16_UINT : ERHIFormat::R32_UINT;

	ImTextureID ToTextureID(IRHIDescriptorTable* table)
	{
		return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(table));
	}
	IRHIDescriptorTable* FromTextureID(ImTextureID id)
	{
		return reinterpret_cast<IRHIDescriptorTable*>(static_cast<std::uintptr_t>(id));
	}

	// 足りなくなったら 2 倍ずつ広げる
	std::size_t GrowCapacity(std::size_t current, std::size_t initial, std::size_t required)
	{
		std::size_t capacity = current == 0 ? initial : current;
		while (capacity < required)
		{
			capacity *= 2;
		}
		return capacity;
	}
}

ImGuiRenderer::ImGuiRenderer(IRHIDevice& device, std::filesystem::path shaderDirectory)
	: m_Device(device), m_ShaderDirectory(std::move(shaderDirectory))
{
	ImGuiIO& io = ImGui::GetIO();
	io.BackendRendererName = "TentativeEngine_RHI";
	// VtxOffset: 頂点が 65536 個を超えても 16 ビットのインデックスで描ける（DrawIndexed の baseVertex を使う）
	// Textures : フォントのアトラスなどを、ImGui の要求に応じてこちらで作る・更新する
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

	m_UploadCommandList = m_Device.CreateCommandList("ImGuiUploadCommandList");
	m_Fence = m_Device.CreateFence(0);
	if (!m_UploadCommandList || !m_Fence)
	{
		throw std::runtime_error("ImGui のコマンドリスト / フェンスの生成に失敗しました");
	}

	CreatePipelineLayout();
	CompileShaders();

	m_Frames.resize(m_Device.GetFramesInFlight());
	for (FrameResources& frame : m_Frames)
	{
		RHIBufferDesc desc{};
		desc.size = kConstantBufferSize;
		desc.usage = ERHIBufferUsage::ConstantBuffer;
		desc.memory = ERHIMemoryType::Upload;
		desc.debugName = "ImGuiConstants";
		frame.constantBuffer = m_Device.CreateBuffer(desc);
		if (!frame.constantBuffer)
		{
			throw std::runtime_error("ImGui の定数バッファの生成に失敗しました");
		}
		// Upload は作ったときからずっとマップしておける
		frame.mappedConstants = frame.constantBuffer->Map();
		if (frame.mappedConstants == nullptr)
		{
			throw std::runtime_error("ImGui の定数バッファのマップに失敗しました");
		}
		EnsureCapacity(frame, kInitialVertexCapacity, kInitialIndexCapacity);
	}
}

ImGuiRenderer::~ImGuiRenderer()
{
	// GPU が使っている途中のリソースを解放しないよう、全部終わるのを待つ
	m_Device.WaitIdle();

	// ImGui が持っているテクスチャ（フォントなど）のうち、こちらで作った分を片付ける
	for (ImTextureData* data : ImGui::GetPlatformIO().Textures)
	{
		if (data->RefCount == 1)
		{
			DestroyTexture(*data);
		}
	}

	ImGuiIO& io = ImGui::GetIO();
	io.BackendRendererName = nullptr;
	io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

// ---------------------------------------------------//
//                    テクスチャ                        //
// ---------------------------------------------------//

void ImGuiRenderer::UpdateTextures(ImDrawData* drawData)
{
	if (drawData == nullptr || drawData->Textures == nullptr)
	{
		return;
	}
	for (ImTextureData* data : *drawData->Textures)
	{
		switch (data->Status)
		{
		case ImTextureStatus_WantCreate:
		case ImTextureStatus_WantUpdates:
			// 更新も作り直しで済ませる。フォントの更新はまれなので、部分更新（UpdateRect）までは要らない
			UploadTexture(*data);
			break;
		case ImTextureStatus_WantDestroy:
			DestroyTexture(*data);
			break;
		default:
			break;
		}
	}
}

void ImGuiRenderer::UploadTexture(ImTextureData& data)
{
	// RHI のテクスチャは RGBA8 だけ扱うので、Alpha8 は白 + アルファに広げる
	const int width = data.Width;
	const int height = data.Height;
	std::vector<std::uint8_t> expanded;
	const void* pixels = data.GetPixels();
	if (data.Format == ImTextureFormat_Alpha8)
	{
		const std::size_t count = static_cast<std::size_t>(width) * height;
		expanded.resize(count * 4);
		const auto* src = static_cast<const std::uint8_t*>(pixels);
		for (std::size_t i = 0; i < count; ++i)
		{
			expanded[i * 4 + 0] = 255;
			expanded[i * 4 + 1] = 255;
			expanded[i * 4 + 2] = 255;
			expanded[i * 4 + 3] = src[i];
		}
		pixels = expanded.data();
	}

	RHITextureDesc desc{};
	desc.width = static_cast<std::uint32_t>(width);
	desc.height = static_cast<std::uint32_t>(height);
	// ImGui の色は sRGB のまま合成する前提なので、_SRGB にしない（読んだ値をそのまま使う）
	desc.format = ERHIFormat::R8G8B8A8_UNORM;
	desc.usage = ERHITextureUsage::ShaderResource;
	desc.debugName = "ImGuiTexture";
	RHITextureRef texture = m_Device.CreateTexture(desc);
	if (!texture)
	{
		throw std::runtime_error("ImGui のテクスチャの生成に失敗しました");
	}

	const std::uint64_t rowPitch = static_cast<std::uint64_t>(width) * 4;
	const RHISubresourceData subresource{ pixels, rowPitch, rowPitch * static_cast<std::uint64_t>(height) };

	m_UploadCommandList->Begin();
	m_UploadCommandList->Transition(texture.get(), ERHIResourceState::Undefined, ERHIResourceState::CopyDst);
	// staging は GPU がコピーし終わるまで持っておく（下の Wait が終わるまで、この関数の中で生きている）
	const RHIBufferRef staging = RHIUploadTexture(m_Device, *m_UploadCommandList, texture.get(), 0, 1, &subresource);
	m_UploadCommandList->Transition(texture.get(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResourcePS);
	m_UploadCommandList->End();
	if (!staging)
	{
		throw std::runtime_error("ImGui のテクスチャのアップロードに失敗しました");
	}
	IRHICommandList* lists[] = { m_UploadCommandList.get() };
	m_Device.Submit(lists, 1, m_Fence.get(), ++m_FenceValue);
	m_Fence->Wait(m_FenceValue);

	// 使用中のテーブルは書き換えられない（IRHIDescriptorTable の決まり）ので、更新でも新しく作って差し替える。
	// 古いものは手放すだけでよい。GPU が使い終わるまでは RHI の遅延解放で残る
	RHIDescriptorTableRef table = m_Device.CreateDescriptorTable(m_PipelineLayout.get(), kTextureSlot);
	if (!table)
	{
		throw std::runtime_error("ImGui のデスクリプタテーブルの生成に失敗しました");
	}
	table->SetTexture(0, texture);

	auto* entry = static_cast<Texture*>(data.BackendUserData);
	if (entry == nullptr)
	{
		entry = new Texture{};
		data.BackendUserData = entry;
	}
	entry->texture = std::move(texture);
	entry->table = std::move(table);

	data.SetTexID(ToTextureID(entry->table.get()));
	data.SetStatus(ImTextureStatus_OK);
}

std::uint64_t ImGuiRenderer::AddTexture(const RHITextureRef& texture)
{
	static_assert(std::is_same_v<ImTextureID, std::uint64_t>, "ImTextureID を std::uint64_t として受け渡している");

	RHIDescriptorTableRef table = m_Device.CreateDescriptorTable(m_PipelineLayout.get(), kTextureSlot);
	if (!table)
	{
		throw std::runtime_error("ImGui のデスクリプタテーブルの生成に失敗しました");
	}
	table->SetTexture(0, texture);
	const ImTextureID id = ToTextureID(table.get());
	m_UserTextures.push_back(std::move(table));
	return id;
}

void ImGuiRenderer::RemoveTexture(std::uint64_t id)
{
	// 手放しても、GPU が使い終わるまでは RHI の遅延解放で残る
	std::erase_if(m_UserTextures, [id](const RHIDescriptorTableRef& table)
		{
			return ToTextureID(table.get()) == id;
		});
}

void ImGuiRenderer::DestroyTexture(ImTextureData& data)
{
	// ここで手放しても、GPU が使い終わるまでは RHI の遅延解放で残る
	delete static_cast<Texture*>(data.BackendUserData);
	data.BackendUserData = nullptr;
	data.SetTexID(ImTextureID_Invalid);
	data.SetStatus(ImTextureStatus_Destroyed);
}

// ---------------------------------------------------//
//                       描画                          //
// ---------------------------------------------------//

void ImGuiRenderer::Record(IRHICommandList& commandList, const ImDrawData* drawData,
	ERHIFormat colorFormat, ERHIFormat depthFormat,
	std::uint32_t frameSlot)
{
	if (drawData == nullptr || drawData->TotalVtxCount == 0)
	{
		return;
	}
	// 最小化中は表示サイズが 0 になる
	const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
	const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
	if (framebufferWidth <= 0.0f || framebufferHeight <= 0.0f)
	{
		return;
	}
	IRHIPipelineState* pipeline = GetPipeline(colorFormat, depthFormat);
	if (pipeline == nullptr)
	{
		return;	// シェーダーのコンパイルに失敗している。ログは出してある
	}

	// ---- GPU に送るデータを書く ---- //
	// このスロットを前回使った GPU の作業は終わっているので、ここで書き換えても描画中のデータは壊れない
	FrameResources& frame = m_Frames[frameSlot];
	EnsureCapacity(frame, static_cast<std::size_t>(drawData->TotalVtxCount),
		static_cast<std::size_t>(drawData->TotalIdxCount));

	// 全リストの頂点とインデックスを 1 本ずつに詰める。リストごとの先頭は下の描画で足していく
	auto* vertexDst = static_cast<ImDrawVert*>(frame.mappedVertices);
	auto* indexDst = static_cast<ImDrawIdx*>(frame.mappedIndices);
	for (const ImDrawList* list : drawData->CmdLists)
	{
		std::memcpy(vertexDst, list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
		std::memcpy(indexDst, list->IdxBuffer.Data, static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
		vertexDst += list->VtxBuffer.Size;
		indexDst += list->IdxBuffer.Size;
	}

	// 画面の範囲（DisplayPos 〜 DisplayPos + DisplaySize）を NDC（-1〜1、y は上向き）へ写す
	const float left = drawData->DisplayPos.x;
	const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
	const float top = drawData->DisplayPos.y;
	const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
	ImGuiConstants constants{};
	constants.scale[0] = 2.0f / (right - left);
	constants.scale[1] = -2.0f / (bottom - top);
	constants.translate[0] = -(right + left) / (right - left);
	constants.translate[1] = (bottom + top) / (bottom - top);
	std::memcpy(frame.mappedConstants, &constants, sizeof(constants));

	// ---- 記録 ---- //
	SetupRenderState(commandList, drawData, frame, pipeline);

	// クリップ矩形は画面の座標なので、DisplayPos を引いてからフレームバッファの倍率を掛ける
	const ImVec2 clipOffset = drawData->DisplayPos;
	const ImVec2 clipScale = drawData->FramebufferScale;
	// 同じテクスチャが続くことが多いので、変わったときだけ結び直す
	IRHIDescriptorTable* boundTable = nullptr;
	// 「描画状態を初めに戻して」という合図の値
	const ImDrawCallback resetRenderState = ImGui::GetPlatformIO().DrawCallback_ResetRenderState;
	std::uint32_t globalVertexOffset = 0;
	std::uint32_t globalIndexOffset = 0;
	for (const ImDrawList* list : drawData->CmdLists)
	{
		for (const ImDrawCmd& cmd : list->CmdBuffer)
		{
			if (cmd.UserCallback != nullptr)
			{
				if (cmd.UserCallback == resetRenderState)
				{
					SetupRenderState(commandList, drawData, frame, pipeline);
					boundTable = nullptr;
				}
				else
				{
					cmd.UserCallback(list, &cmd);
				}
				continue;
			}

			// 画面の外にはみ出した分を削る。何も残らなければ描かない
			const float minX = std::max((cmd.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f);
			const float minY = std::max((cmd.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f);
			const float maxX = std::min((cmd.ClipRect.z - clipOffset.x) * clipScale.x, framebufferWidth);
			const float maxY = std::min((cmd.ClipRect.w - clipOffset.y) * clipScale.y, framebufferHeight);
			if (maxX <= minX || maxY <= minY)
			{
				continue;
			}
			commandList.SetScissor({ static_cast<std::int32_t>(minX), static_cast<std::int32_t>(minY),
									 static_cast<std::int32_t>(maxX), static_cast<std::int32_t>(maxY) });

			// TexID は描くときに ImTextureData から引く（UpdateTextures で差し替えた後の値になる）
			IRHIDescriptorTable* table = FromTextureID(cmd.GetTexID());
			if (table != nullptr && table != boundTable)
			{
				commandList.SetDescriptorTable(kTextureSlot, table);
				boundTable = table;
			}

			commandList.DrawIndexed(cmd.ElemCount, 1,
				cmd.IdxOffset + globalIndexOffset,
				static_cast<std::int32_t>(cmd.VtxOffset + globalVertexOffset));
		}
		globalVertexOffset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
		globalIndexOffset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
	}
	// scissor は戻さない。Renderer はパスの最初に毎フレーム設定し直している
}

void ImGuiRenderer::SetupRenderState(IRHICommandList& commandList, const ImDrawData* drawData,
	const FrameResources& frame, IRHIPipelineState* pipeline)
{
	const float width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
	const float height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
	commandList.SetViewport({ 0.0f, 0.0f, width, height, 0.0f, 1.0f });
	commandList.SetPipelineState(pipeline);
	// PSO の後で結ぶ（RHI の決まり）
	commandList.SetConstantBuffer(kConstantsSlot, frame.constantBuffer.get());
	commandList.SetVertexBuffer(0, frame.vertexBuffer.get());
	commandList.SetIndexBuffer(frame.indexBuffer.get(), kIndexFormat);
}

// ---------------------------------------------------//
//                       作成                          //
// ---------------------------------------------------//

void ImGuiRenderer::CreatePipelineLayout()
{
	RHIPipelineLayoutDesc layoutDesc{};
	layoutDesc.slots = {
		// b0: 座標の変換。頂点シェーダーだけが読む
		{ ERHIBindingType::ConstantBuffer, 0, 0, 1, ERHIShaderStage::Vertex },
		// t0: フォントのアトラスや、ImGui::Image に渡したテクスチャ
		{ ERHIBindingType::TextureTable, 0, 0, 1, ERHIShaderStage::Pixel },
	};
	// s0: 文字の縁をなめらかにするため線形補間。端で繰り返すと隣のグリフがにじむので Clamp
	ERHIStaticSamplerDesc sampler{};
	sampler.shaderRegister = 0;
	sampler.filter = ERHIFilter::Linear;
	sampler.address = ERHIAddressMode::Clamp;
	sampler.visibility = ERHIShaderStage::Pixel;
	layoutDesc.staticSamplers = { sampler };
	layoutDesc.debugName = "ImGuiLayout";

	m_PipelineLayout = m_Device.CreatePipelineLayout(layoutDesc);
	if (!m_PipelineLayout)
	{
		throw std::runtime_error("ImGui のパイプラインレイアウトの生成に失敗しました");
	}
}

void ImGuiRenderer::CompileShaders()
{
	const std::filesystem::path path = m_ShaderDirectory / kShaderFile;
	const ERHIBackend backend = m_Device.GetBackend();
	m_VertexShader = ShaderCompiler::CompileFile(path, "VSMain", ShaderCompiler::Stage::Vertex, backend);
	m_PixelShader = ShaderCompiler::CompileFile(path, "PSMain", ShaderCompiler::Stage::Pixel, backend);
	if (m_VertexShader.empty() || m_PixelShader.empty())
	{
		// UI が無くてもエンジンは動くので止めない。Record が何も描かなくなるだけ
		LogError("[ImGuiRenderer] シェーダーのコンパイルに失敗しました");
	}
}

IRHIPipelineState* ImGuiRenderer::GetPipeline(ERHIFormat colorFormat, ERHIFormat depthFormat)
{
	for (const PipelineEntry& entry : m_Pipelines)
	{
		if (entry.colorFormat == colorFormat && entry.depthFormat == depthFormat)
		{
			return entry.pipeline.get();
		}
	}
	if (m_VertexShader.empty() || m_PixelShader.empty())
	{
		return nullptr;
	}

	RHIGraphicsPipelineDesc psoDesc{};
	psoDesc.layout = m_PipelineLayout;
	psoDesc.vs = { m_VertexShader.data(), m_VertexShader.size(), "VSMain" };
	psoDesc.ps = { m_PixelShader.data(), m_PixelShader.size(), "PSMain" };
	// ImDrawVert の並びそのまま（並び順 = シェーダーの VK_LOCATION の番号）
	psoDesc.vertexAttributes = {
		{ "POSITION", 0, ERHIFormat::R32G32_FLOAT,   0, static_cast<std::uint32_t>(offsetof(ImDrawVert, pos)) },
		{ "TEXCOORD", 0, ERHIFormat::R32G32_FLOAT,   0, static_cast<std::uint32_t>(offsetof(ImDrawVert, uv)) },
		{ "COLOR",    0, ERHIFormat::R8G8B8A8_UNORM, 0, static_cast<std::uint32_t>(offsetof(ImDrawVert, col)) },
	};
	psoDesc.vertexBuffers = { { sizeof(ImDrawVert), false } };
	// UI は表裏を気にせず、奥行きも使わない。重ねた順に半透明で混ぜる
	psoDesc.rasterizer.cullMode = ERHICullMode::None;
	psoDesc.depthStencil.depthTestEnable = false;
	psoDesc.depthStencil.depthWriteEnable = false;
	psoDesc.blend[0] = RHIBlendDesc::AlphaBlend();
	// アルファは重ねた後も不透明のままになるよう One / InvSrcAlpha にする（ImGui 公式と同じ）
	psoDesc.blend[0].srcAlpha = ERHIBlendFactor::One;
	psoDesc.blend[0].dstAlpha = ERHIBlendFactor::InvSrcAlpha;
	psoDesc.numRenderTargets = 1;
	psoDesc.rtvFormat[0] = colorFormat;
	// テストはしないが、深度付きのパスで使うときはフォーマットを揃えておく（D3D12 / Vulkan は不一致を許さない）
	psoDesc.dsvFormat = depthFormat;
	psoDesc.debugName = "ImGuiPSO";

	RHIPipelineStateRef pipeline = m_Device.CreatePipelineState(psoDesc);
	if (!pipeline)
	{
		LogError("[ImGuiRenderer] PSO の生成に失敗しました");
		return nullptr;
	}
	m_Pipelines.push_back({ colorFormat, depthFormat, std::move(pipeline) });
	return m_Pipelines.back().pipeline.get();
}

void ImGuiRenderer::EnsureCapacity(FrameResources& frame, std::size_t vertexCount, std::size_t indexCount)
{
	// 古いバッファは手放すだけでよい。GPU が使い終わるまでは RHI の遅延解放で残る
	if (vertexCount > frame.vertexCapacity)
	{
		const std::size_t capacity = GrowCapacity(frame.vertexCapacity, kInitialVertexCapacity, vertexCount);
		RHIBufferDesc desc{};
		desc.size = capacity * sizeof(ImDrawVert);
		desc.usage = ERHIBufferUsage::VertexBuffer;
		desc.memory = ERHIMemoryType::Upload;
		desc.debugName = "ImGuiVB";
		frame.vertexBuffer = m_Device.CreateBuffer(desc);
		frame.mappedVertices = frame.vertexBuffer ? frame.vertexBuffer->Map() : nullptr;
		if (frame.mappedVertices == nullptr)
		{
			throw std::runtime_error("ImGui の頂点バッファの生成に失敗しました");
		}
		frame.vertexCapacity = capacity;
	}
	if (indexCount > frame.indexCapacity)
	{
		const std::size_t capacity = GrowCapacity(frame.indexCapacity, kInitialIndexCapacity, indexCount);
		RHIBufferDesc desc{};
		desc.size = capacity * sizeof(ImDrawIdx);
		desc.usage = ERHIBufferUsage::IndexBuffer;
		desc.memory = ERHIMemoryType::Upload;
		desc.debugName = "ImGuiIB";
		frame.indexBuffer = m_Device.CreateBuffer(desc);
		frame.mappedIndices = frame.indexBuffer ? frame.indexBuffer->Map() : nullptr;
		if (frame.mappedIndices == nullptr)
		{
			throw std::runtime_error("ImGui のインデックスバッファの生成に失敗しました");
		}
		frame.indexCapacity = capacity;
	}
}
