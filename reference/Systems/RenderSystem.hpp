#pragma once

#include "../Defines.hpp"
#include "../RenderContext.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

class World;
class Mesh;
class Material;
class FramePipeline;
struct ID3D12PipelineState;

enum class DrawFilter : uint8_t
{
	ALL,
	OPAQUEONLY,
	TRANSPARENTONLY,
	OPAQUE_NOTOON,		// 不透明のうちトゥーン以外(デファードのG-Buffer用)
	OPAQUE_TOON,		// 不透明のうちトゥーンだけ(フォワードで重ねる用)
	REFLECTION,			// 平面反射に映すもの(ReflectionCaster が付いたEntityだけ)
};

// トゥーン系シェーダーか。デファードではG-Bufferに入れず、
// フォワードで従来のシェーダーのまま描くために使う
inline bool IsToonShader(const std::string& name)
{
	return name.find("Toon") != std::string::npos;
}

#ifdef _FRAMEPIPELINE
/// @brief そのフレームで確定した描画1件
struct FO_DrawItem
{
	float4x4 world{};

	std::shared_ptr<Mesh>     mesh;
	std::shared_ptr<Material> material;
	std::vector<std::shared_ptr<Material>> materials;   // サブメッシュ用(空なら単体)
	std::string shaderName;

	// ボーンパレット。スキン無しは共有の単位行列を指すのでフレームメモリを食わない
	const BoneCB* boneCb = nullptr;

	// 頂点モーフ。nullptr なら無し
	const DirectX::XMFLOAT3* morphOffsets = nullptr;
	UINT morphVertexCount = 0;

	bool isReflectionCaster = true;
};

/// @brief スキン無しエンティティが共有する単位行列パレット
const BoneCB& IdentityBoneCB();
#endif

class RenderSystem
{
public:
#ifdef _FRAMEPIPELINE
	/// @brief World を走査して FO_DrawItem を積む(Game フェーズで呼ぶ)
	/// @note ここを通したあと Render 側は World を一切読まない
	static void Publish(_In_ World& world, _In_ FramePipeline& fp);
#endif

	void Draw(
		_In_ World& world,
		_In_ const RenderContext& renderContext,
		_In_ ID3D12PipelineState* overidePso = nullptr,
		_In_ DrawFilter filter = DrawFilter::ALL);

private:
	/// psoSuffix → (素のシェーダー名 → 解決後のパス名)
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_PassCache;
};