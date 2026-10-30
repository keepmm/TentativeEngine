/**
 * \file ModelUpload.hpp
 * \brief 読み込んだモデル（CPU 側）を Renderer のメッシュにする
 */
#pragma once

#include "Asset/ModelData.hpp"
#include "Renderer/MeshRenderer.hpp"

class Renderer;

namespace TE {

    // Renderer は Asset を知らないので、両方を知っているここで橋渡しする。
    // テクスチャ（ベースカラー / 法線 / エミッシブ）とマテリアルも、このモデルのぶんだけ作る
    MeshHandle CreateMeshFromModel(Renderer& renderer, const ModelCpuData& model);

}  // namespace TE
