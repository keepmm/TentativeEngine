/*! ************************************************************
 * \file   Material.hpp
 * \brief  マテリアル(パラメータ + テクスチャ + 描画時のバインド)
 *
 * 作成者 keeep
 * 更新履歴	9.12 パラメータを MaterialParams、テクスチャを MaterialTextures へ分離
 *
 * \note 責務はここまで:「Apply でコマンドリストへ流す」
 *       - 見た目の値       → MaterialParams(継承しているので mat.roughness で触れる)
 *       - テクスチャ / SRV → MaterialTextures
 *       - 画像の読み込み   → TextureLoader
 * *********************************************************************/
#pragma once

#include "DirectX.hpp"
#include "Defines.hpp"
#include <cstdint>
#include "RenderContext.hpp"
#include "ShaderTypes.hpp"
#include "MaterialParams.hpp"
#include "MaterialTextures.hpp"

class ConstantBufferAllocator;

class Material : public MaterialParams
{
public:
	/// @brief 初期化(2x2 白 + 既定ランプ + PSO テーブルの取得)
	void Init();

	// =============== テクスチャの設定 =============== //

	/// @brief ファイルからベースカラーを設定する
	bool SetTextureFromFile(_In_ const std::wstring& filePath);

	/// @brief メモリからベースカラーを設定する
	bool SetTextureFromMemory(_In_ const std::uint8_t* data, size_t size);

	/// @brief ベースカラーを単色で塗りつぶす
	/// @param color 塗る色
	/// @return 成功なら true
	/// @note UI の板など、画像を用意せず色だけ出したいとき用。
	///       アップロードバッファを書き換えるだけなのでリソースは作り直さない
	bool SetSolidColor(_In_ const COLOR& color);

	/// @brief トゥーンランプテクスチャの設定
	bool SetToonRampTexture(_In_ const std::wstring& filepath);

	bool SetNormalTexture(_In_ const std::wstring& path);
	bool SetMetalTexture(_In_ const std::wstring& path);
	bool SetRoughTexture(_In_ const std::wstring& path);

	/// @brief 他マテリアルのベースカラーを共有する(GPUリソースだけ)
	void ShareDiffuseTexture(_In_ const Material& src);

	// =============== RGBA 生データからの生成 =============== //

	bool CreateTextureFromRGBA(_In_ UINT width, _In_ UINT height, _In_ const std::uint8_t* data);
	bool CreateNormalFromRGBA(UINT w, UINT h, const std::uint8_t* p);
	bool CreateMetalFromRGBA(UINT w, UINT h, const std::uint8_t* p);
	bool CreateRoughFromRGBA(UINT w, UINT h, const std::uint8_t* p);
	bool CreateEmissiveFromRGBA(UINT w, UINT h, const std::uint8_t* p);
	bool CreateOcclusionFromRGBA(UINT w, UINT h, const std::uint8_t* p);

	// =============== 描画 =============== //

	/// @brief マテリアルを適用する
	/// @param commandList コマンドリスト
	/// @param world world行列
	/// @param view view行列
	/// @param projection proj行列
	/// @param wireframe wireframe描画するか
	/// @param frameIndex 描画インデックス
	/// @param cbAlloc 定数バッファアロケータ
	/// @param shaderName シェーダーの名前
	/// @param overridePso 使用するPSO
	void Apply(
		_In_ ID3D12GraphicsCommandList* commandList,
		_In_ const float4x4& world,
		_In_ const float4x4& view,
		_In_ const float4x4& projection,
		bool wireframe,
		UINT frameIndex = 0,
		_In_ ConstantBufferAllocator* cbAlloc = nullptr,
		_In_ const std::string& shaderName = "",
		_In_opt_ ID3D12PipelineState* overridePso = nullptr);

	/// @brief pending なテクスチャのアップロードをコマンドリストへ積む
	void UpdateTextureIfNeeded(_In_ ID3D12GraphicsCommandList* commandList);

	/// @brief テクスチャ群への直接アクセス(スロット指定で触りたいとき用)
	MaterialTextures& Textures() noexcept { return m_Textures; }
	const MaterialTextures& Textures() const noexcept { return m_Textures; }

private:
	static constexpr UINT FRAME_COUNT = RTV_NUM;

	void BuildPerFrame(const float4x4& view, const float4x4& projection, _Out_ FrameCB* out) const;
	void BuildPerObject(const float4x4& world, _Out_ ObjectCB* out) const;

	MaterialTextures m_Textures;

	DirectXApp::PipelineStateTable m_PipelineStates;
	ComPtr<ID3D12PipelineState> m_WirePso;
};
