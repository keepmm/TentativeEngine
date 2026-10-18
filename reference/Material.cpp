/*! ************************************************************
 * \file   Material.cpp
 * \brief  マテリアル(パラメータ + テクスチャ + 描画時のバインド)
 *
 * 作成者 keeep
 * 更新履歴	9.12 パラメータを MaterialParams、テクスチャを MaterialTextures へ分離
 * *********************************************************************/
#include "Material.hpp"

#include "d3dx12.h"
#include "Logger.hpp"
#include "ConstantBufferAllocator.hpp"
#include "TextureLoader.hpp"
#include "Util.hpp"
#include "Time.hpp"

namespace
{
	/// @brief 読んだファイルを Assets 相対で返す(.mat に書くため)
	std::string ToSourcePath(const std::wstring& file)
	{
		return MakeAssetRelative(std::filesystem::path(file).generic_string());
	}
}

void Material::Init()
{
	// デバイスが無い場合は初期化しない
	if (APP->GetDevice() == nullptr) return;

	m_Textures.CreateWhite();
	m_Textures.CreateDefaultRamp();

	m_PipelineStates = APP->GetPipelineStates();
	m_WirePso = APP->GetPipelineStateWireFrame();
}

// ------------------------------------------------------------------ //
//                          テクスチャの設定                           //
// ------------------------------------------------------------------ //

bool Material::SetTextureFromFile(const std::wstring& filePath)
{
	if (APP->GetDevice() == nullptr)
	{
		LOG->LogError("SetTextureFromFile: device is null");
		return false;
	}

	if (!m_Textures.LoadFromFile(TexSlot::Albedo, filePath))
	{
		LOG->LogError("SetTextureFromFile: failed " + WideToUtf8(filePath));
		return false;
	}

	m_Textures.SetSourcePath(TexSlot::Albedo, ToSourcePath(filePath));
	LOG->LogInfo("SetTextureFromFile: texture loaded");
	return true;
}

bool Material::SetTextureFromMemory(const std::uint8_t* data, size_t size)
{
	if (APP->GetDevice() == nullptr) { LOG->LogError("SetTextureFromMemory: device is null"); return false; }
	if (data == nullptr || size == 0) { LOG->LogError("SetTextureFromMemory: invalid data"); return false; }

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (FAILED(TextureLoader::LoadFromMemory(data, size, metadata, image)))
	{
		LOG->LogError("SetTextureFromMemory: LoadFromWICMemory failed");
		return false;
	}

	// メモリ経由のベースカラーは sRGB として読む
	return m_Textures.UploadImage(TexSlot::Albedo, image.GetImage(0, 0, 0), metadata, true);
}

bool Material::SetSolidColor(const COLOR& color)
{
	return m_Textures.SetSolidColor(color);
}

bool Material::SetToonRampTexture(const std::wstring& filepath)
{
	if (APP->GetDevice() == nullptr) return false;

	DirectX::TexMetadata metadata{};
	DirectX::ScratchImage image{};
	if (FAILED(TextureLoader::LoadWic(filepath, metadata, image)))
	{
		LOG->LogError("SetToonRampTexture: LoadFromWICFileが失敗しました");
		return false;
	}

	if (!m_Textures.UploadImage(TexSlot::Ramp, image.GetImage(0, 0, 0), metadata)) return false;

	m_Textures.SetSourcePath(TexSlot::Ramp, ToSourcePath(filepath));
	m_Textures.MarkCustomRamp();
	return true;
}

bool Material::SetNormalTexture(const std::wstring& path)
{
	if (!m_Textures.LoadFromFile(TexSlot::Normal, path)) return false;
	m_Textures.SetSourcePath(TexSlot::Normal, ToSourcePath(path));
	return true;
}

bool Material::SetMetalTexture(const std::wstring& path)
{
	if (!m_Textures.LoadFromFile(TexSlot::Metal, path)) return false;
	m_Textures.SetSourcePath(TexSlot::Metal, ToSourcePath(path));
	return true;
}

bool Material::SetRoughTexture(const std::wstring& path)
{
	if (!m_Textures.LoadFromFile(TexSlot::Rough, path)) return false;
	m_Textures.SetSourcePath(TexSlot::Rough, ToSourcePath(path));
	return true;
}

void Material::ShareDiffuseTexture(const Material& src)
{
	m_Textures.ShareAlbedo(src.m_Textures);
}

// ------------------------------------------------------------------ //
//                      RGBA 生データからの生成                        //
// ------------------------------------------------------------------ //

bool Material::CreateTextureFromRGBA(UINT width, UINT height, const std::uint8_t* rgba)
{
	// ベースカラーは sRGB エンコード済み。GPU 側でリニアへ展開させる
	return m_Textures.CreateFromRGBA(TexSlot::Albedo, width, height, rgba,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
}

bool Material::CreateNormalFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	return m_Textures.CreateFromRGBA(TexSlot::Normal, w, h, p);
}

bool Material::CreateMetalFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	return m_Textures.CreateFromRGBA(TexSlot::Metal, w, h, p);
}

bool Material::CreateRoughFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	return m_Textures.CreateFromRGBA(TexSlot::Rough, w, h, p);
}

bool Material::CreateEmissiveFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	// エミッシブは sRGB エンコード済みなので展開して読む
	return m_Textures.CreateFromRGBA(TexSlot::Emissive, w, h, p,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
}

bool Material::CreateOcclusionFromRGBA(UINT w, UINT h, const std::uint8_t* p)
{
	// AO はリニアデータ
	return m_Textures.CreateFromRGBA(TexSlot::Occlusion, w, h, p);
}

void Material::UpdateTextureIfNeeded(ID3D12GraphicsCommandList* commandList)
{
	m_Textures.Flush(commandList);
}

// ------------------------------------------------------------------ //
//                              描画                                   //
// ------------------------------------------------------------------ //

void Material::BuildPerFrame(const float4x4& view, const float4x4& projection, FrameCB* out) const
{
	FrameCB data{};
	const auto v = DirectX::XMLoadFloat4x4(&view);
	const auto p = DirectX::XMLoadFloat4x4(&projection);
	DirectX::XMStoreFloat4x4(&data.viewProj, DirectX::XMMatrixTranspose(v * p));

	// カメラのワールド座標 = view の逆行列の平行移動
	const auto invV = DirectX::XMMatrixInverse(nullptr, v);
	DirectX::XMFLOAT4X4 iv; DirectX::XMStoreFloat4x4(&iv, invV);
	// w には経過秒を入れる(水面の波などが時間を使う)
	data.cameraPos = { iv._41, iv._42, iv._43, TIME->GetTotalTime() };

	*out = data;
}

void Material::BuildPerObject(const float4x4& world, ObjectCB* out) const
{
	ObjectCB data{};

	// 行列の作成
	const auto w = DirectX::XMLoadFloat4x4(&world);

	// 転置して格納
	DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixTranspose(w));

	*out = data;
}

void Material::Apply(
	ID3D12GraphicsCommandList* commandList,
	const float4x4& world,
	const float4x4& view,
	const float4x4& projection,
	bool wireframe,
	UINT frameIndex,
	ConstantBufferAllocator* cbAlloc,
	const std::string& shaderName,
	ID3D12PipelineState* overridePso)
{
	// コマンドリストが空の場合は適用しない
	if (commandList == nullptr) return;

	m_Textures.Flush(commandList);
	m_Textures.BindGlobals(reflectStrength);

	if (auto* heap = m_Textures.Heap())
	{
		ID3D12DescriptorHeap* heaps[] = { heap };
		commandList->SetDescriptorHeaps(_countof(heaps), heaps);
		commandList->SetGraphicsRootDescriptorTable(4, heap->GetGPUDescriptorHandleForHeapStart());
	}

	// ------------------------------- //
	// 定数バッファ(アロケータから確保) //
	// ------------------------------- //
	if (cbAlloc == nullptr) return;

	const UINT frameSlot = frameIndex % FRAME_COUNT;

	// b0
	FrameCB fdata = {};
	BuildPerFrame(view, projection, &fdata);
	const D3D12_GPU_VIRTUAL_ADDRESS b0 = cbAlloc->Allocate(frameSlot, &fdata, sizeof(FrameCB));

	// b1
	ObjectCB odata = {};
	BuildPerObject(world, &odata);
	const D3D12_GPU_VIRTUAL_ADDRESS b1 = cbAlloc->Allocate(frameSlot, &odata, sizeof(ObjectCB));

	// b3 : パラメータ由来 → テクスチャ由来 の順に詰める(書き込むフィールドは重ならない)
	MaterialCB mdata{};
	MaterialParams::FillCB(mdata);
	m_Textures.FillCB(mdata);
	const D3D12_GPU_VIRTUAL_ADDRESS b3 = cbAlloc->Allocate(frameSlot, &mdata, sizeof(MaterialCB));

	if (b0 == 0 || b1 == 0 || b3 == 0)
	{
		// リングが溢れた等で確保できない場合は描画しない
		return;
	}

	// ルートパラメータに定数バッファのGPU仮想アドレスをセット
	commandList->SetGraphicsRootConstantBufferView(0, b0);
	commandList->SetGraphicsRootConstantBufferView(1, b1);
	commandList->SetGraphicsRootConstantBufferView(3, b3);

	// PSOの選択
	ID3D12PipelineState* pso = overridePso;
	if (!pso && wireframe) pso = m_WirePso.Get();
	if (!pso)              pso = APP->GetPipelineStateByName(shaderName);
	// 安全網
	static const std::string fallback = "Basic";
	if (!pso)              pso = APP->GetPipelineStateByName(fallback);
	if (!pso) return;
	commandList->SetPipelineState(pso);
}
