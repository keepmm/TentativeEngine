/*****************************************************************//**
 * \file   ModelData.hpp
 * \brief  読み込んだモデルのCPU側のデータ
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "../Math/Color.hpp"
#include "Math/Matrix4x4.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Vector2.hpp"
#include "Math/Vector3.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace TE
{
	/// @brief 描画に使う属性
	/// ボーンの情報は SkinVertexに分ける
	struct ModelVertex
	{
		Vector3 position;
		Vector3 normal;
		Vector2 uv;
		Vector3 tangent;
	};

	/// @brief スキン頂点を表す構造体です
	struct SkinVertex
	{
		std::array<std::uint16_t, 4> boneIndices{};
		std::array<float, 4> boneWeights{};
	};

	struct TextureData 
	{
		struct Mip
		{
			std::size_t offset = 0;	// pixelsの先頭からのバイト位置
			std::uint32_t width = 0;
			std::uint32_t height = 0;
		};
		std::vector<std::uint8_t> pixels;
		std::vector<Mip> mips;
	};

	struct ModelMaterial
	{
		std::string name;
		Color baseColorFactor = Color::white;
		Color emissiveFactor = Color::black;
		std::shared_ptr<const TextureData> baseColor;
		std::shared_ptr<const TextureData> normal;
		std::shared_ptr<const TextureData> emissive;
	};

	struct ModelSubMesh
	{
		std::uint32_t firstIndex = 0;
		std::uint32_t indexCount = 0;
		std::uint32_t materialIndex = 0;
	};

	struct BoneNode
	{
		std::string name;
		int parentIndex = -1;		// -1はルート
		Matrix4x4 localTransform;	// 親の空間での行列
		std::vector<int> children;
	};

	struct Skeleton
	{
		std::vector<BoneNode> nodes; // [0]がルート
		std::unordered_map<std::string, int> nameToIndex;
	};

	struct SkinData
	{
		std::vector<std::string> boneNames;
		std::vector<Matrix4x4> offsetMatrices;	// モデル空間 -> ボーンの空間(逆バインド行列)
		std::unordered_map<std::string, std::uint16_t> boneNameToIndex;
	};

	struct KeyFrame
	{
		float time = 0.0f;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale = Vector3::one;
	};

	struct BoneAnimationChannel
	{
		std::string boneName;
		std::vector<KeyFrame> keyFrames;
	};

	struct AnimationClip
	{
		std::string name;
		float duration = 0.0f;
		std::vector<BoneAnimationChannel> channels;
	};

	struct ModelCpuData
	{
		std::vector<ModelVertex> vertices;
		std::vector<SkinVertex> skinVertices;
		std::vector<std::uint32_t> indices;
		std::vector<ModelSubMesh> subMeshes;
		std::vector<ModelMaterial> materials;
		Skeleton skeleton;
		SkinData skin;
		std::vector<AnimationClip> clips;
	};
}
