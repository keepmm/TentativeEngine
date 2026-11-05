/*****************************************************************//**
 * \file   ImGuiRenderer.hpp
 * \brief  ImGuiの描画データをRHIで描く
 * 
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#pragma once

#include "RHI/RHITypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

struct ImDrawData;
struct ImTextureData;

class ImGuiRenderer
{
public:
	ImGuiRenderer(IRHIDevice& device, std::filesystem::path shaderDirectory);
	~ImGuiRenderer();

	ImGuiRenderer(const ImGuiRenderer&) = delete;
	ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

	static constexpr const char* kShaderFile = "ImGui.hlsl";

	void UpdateTextures(ImDrawData* drawData);

	/// @brief 外で作ったテクスチャ（Scene View の RT など）を ImGui::Image に渡せる ID にする
	/// @note  ImTextureID は前方宣言できないので、中身と同じ std::uint64_t で受け渡す。
	///        ShaderResourcePS の状態で読まれる。作り直したら RemoveTexture で古い方を外す
	std::uint64_t AddTexture(const RHITextureRef& texture);
	void RemoveTexture(std::uint64_t id);

	void Record(IRHICommandList& commandList, const ImDrawData* drawData,
		ERHIFormat colorFormat,ERHIFormat depthFotmat,
		std::uint32_t frameSlot);
private:
	/// @brief フレームスロットごとに持つもの。
	/// GPUが読んでいる間にCPUが書き換えないように分ける
	struct FrameResources
	{
		RHIBufferRef vertexBuffer;
		RHIBufferRef indexBuffer;
		RHIBufferRef constantBuffer;
		void* mappedVertices = nullptr;
		void* mappedIndices = nullptr;
		void* mappedConstants = nullptr;
		std::size_t vertexCapacity = 0;
		std::size_t indexCapacity = 0;
	};

	/// @brief ImTextureData::BackendUserDataに置く
	/// @brief ImTextureIDにはテーブルアドレスを入れる
	struct Texture
	{
		RHITextureRef texture;
		RHIDescriptorTableRef table;
	};

	struct PipelineEntry
	{
		ERHIFormat colorFormat = ERHIFormat::Unknown;
		ERHIFormat depthFormat = ERHIFormat::Unknown;
		RHIPipelineStateRef pipeline;
	};

	void CreatePipelineLayout();
	void CompileShaders();
	IRHIPipelineState* GetPipeline(ERHIFormat colorFormat, ERHIFormat depthFormat);
	void EnsureCapacity(FrameResources& frame, std::size_t vertexCount, std::size_t indexCount);
	void UploadTexture(ImTextureData& data);
	void DestroyTexture(ImTextureData& data);
	void SetupRenderState(IRHICommandList& commandList, const ImDrawData* drawData,
		const FrameResources& frame, IRHIPipelineState* pipeline);

	IRHIDevice& m_Device;
	std::filesystem::path m_ShaderDirectory;

	// テクスチャのアップロード用。毎フレームの記録とは別に、その場で送って待つ
	RHICommandListRef m_UploadCommandList;
	RHIFenceRef m_Fence;
	std::uint64_t m_FenceValue = 0;

	RHIPipelineLayoutRef m_PipelineLayout;
	// PSO を後から別のフォーマットで作れるよう、コンパイル結果は持っておく
	std::vector<std::uint8_t> m_VertexShader;
	std::vector<std::uint8_t> m_PixelShader;
	std::vector<PipelineEntry> m_Pipelines;

	std::vector<FrameResources> m_Frames;  // 添字 = frameSlot

	// AddTexture で作ったテーブル。ImGui が持つテクスチャ（フォント）とは別に管理する。数個なので vector で足りる
	std::vector<RHIDescriptorTableRef> m_UserTextures;
};
