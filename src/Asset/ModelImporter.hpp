/*****************************************************************//**
 * \file   ModelImporter.hpp
 * \brief  assimp で FBX / glTF / objを読み込んで GPU に送る前の形にする
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once


#include "ModelData.hpp"

#include <filesystem>
#include <optional>

namespace TE
{
	/// @brief 画像のでコードまで済ませる
	std::optional<ModelCpuData> ImportModel(const std::filesystem::path& path,
		float scale = 1.0f);
}
