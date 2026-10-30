/**
 * \file ModelUpload.cpp
 * \brief 読み込んだモデル（CPU 側）を Renderer のメッシュにする
 */
#include "Renderer/ModelUpload.hpp"

#include "Renderer/Renderer.hpp"

#include <map>
#include <utility>
#include <vector>

namespace TE {

    namespace {

        // TextureData（ミップ全段を 1 本の配列に詰めたもの）を、Renderer が受け取る形に並べ直して送る
        TextureHandle UploadTexture(Renderer& renderer, const TextureData& data, bool srgb) {
            std::vector<TextureMipDesc> mips;
            mips.reserve(data.mips.size());
            for (const TextureData::Mip& mip : data.mips) {
                mips.push_back({ data.pixels.data() + mip.offset, mip.width, mip.height });
            }
            return renderer.CreateTexture(mips, srgb, "ModelTexture");
        }

    }  // namespace

    MeshHandle CreateMeshFromModel(Renderer& renderer, const ModelCpuData& model) {
        // ---- テクスチャ ----
        // ModelImporter は同じ画像ファイルに同じ TextureData を共有させているので、ポインタで見分けて 1 回だけ GPU に作る。
        // ただし同じ画像でも「色」として使うか「数値」として使うかで形式（sRGB かどうか）が変わるので、それもキーに含める
        std::map<std::pair<const TextureData*, bool>, TextureHandle> uploaded;
        const auto getTexture = [&](const std::shared_ptr<const TextureData>& data, bool srgb) -> TextureHandle {
            if (!data || data->mips.empty()) {
                return {};  // 無効な番号。マテリアルでは既定のテクスチャになる
            }
            const auto key = std::make_pair(data.get(), srgb);
            if (const auto it = uploaded.find(key); it != uploaded.end()) {
                return it->second;
            }
            const TextureHandle handle = UploadTexture(renderer, *data, srgb);
            uploaded.emplace(key, handle);
            return handle;
        };

        // ---- マテリアル ----
        // 添字は ModelMaterial と同じにしておき、サブメッシュの materialIndex でそのまま引けるようにする
        std::vector<MaterialHandle> materials;
        materials.reserve(model.materials.size());
        for (const ModelMaterial& source : model.materials) {
            MaterialDesc desc;
            desc.baseColor = getTexture(source.baseColor, true);
            desc.normal = getTexture(source.normal, false);  // 法線は「数値」なので sRGB にしない
            desc.emissive = getTexture(source.emissive, true);
            desc.baseColorFactor = source.baseColorFactor;
            desc.emissiveFactor = source.emissiveFactor;
            materials.push_back(renderer.CreateMaterial(desc));
        }

        // ---- 頂点 ----
        // 並びは同じだが別の型なので、1 個ずつ写す（型をまたいだ memcpy に頼らない）
        std::vector<MeshVertex> vertices;
        vertices.reserve(model.vertices.size());
        for (const ModelVertex& v : model.vertices) {
            vertices.push_back({ v.position, v.normal, v.uv, v.tangent });
        }

        // ---- サブメッシュ ----
        std::vector<SubMeshDesc> subMeshes;
        subMeshes.reserve(model.subMeshes.size());
        for (const ModelSubMesh& subMesh : model.subMeshes) {
            // 番号が範囲外のときは無効なまま渡す（Renderer が既定のマテリアルに差し替える）
            const MaterialHandle material = subMesh.materialIndex < materials.size()
                ? materials[subMesh.materialIndex]
                : MaterialHandle{};
            subMeshes.push_back({ subMesh.firstIndex, subMesh.indexCount, material });
        }

        return renderer.CreateMesh(vertices, model.indices, subMeshes);
    }

}  // namespace TE
