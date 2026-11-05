/**
 * \file ModelImporter.cpp
 * \brief assimp と DirectXTex でモデルを読む
 */
#include "Asset/ModelImporter.hpp"

#include "Core/Log.hpp"
#include "Core/Platform.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#if TE_PLATFORM_WINDOWS
#include <DirectXTex.h>
#else
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <vector>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace TE {
    namespace {

        // assimp は UTF-8 の文字列でパスをやり取りする。std::string のまま path にすると
        // ANSI として解釈され、日本語のパスが化ける
        std::string ToUtf8(const std::filesystem::path& path) {
            const std::u8string s = path.u8string();
            return { s.begin(), s.end() };
        }

        std::filesystem::path FromUtf8(const char* s) {
            return std::filesystem::path(reinterpret_cast<const char8_t*>(s));
        }

        // assimp の行列は列ベクトル用（a4 が平行移動の x）。エンジンは行ベクトルなので転置して入れる
        Matrix4x4 ToMatrix(const aiMatrix4x4& a) {
            Matrix4x4 r;
            r.m[0][0] = a.a1; r.m[0][1] = a.b1; r.m[0][2] = a.c1; r.m[0][3] = a.d1;
            r.m[1][0] = a.a2; r.m[1][1] = a.b2; r.m[1][2] = a.c2; r.m[1][3] = a.d2;
            r.m[2][0] = a.a3; r.m[2][1] = a.b3; r.m[2][2] = a.c3; r.m[2][3] = a.d3;
            r.m[3][0] = a.a4; r.m[3][1] = a.b4; r.m[3][2] = a.c4; r.m[3][3] = a.d4;
            return r;
        }

        // ---------------------------------------------------//
        //                  スケルトン・スキン                    //
        // ---------------------------------------------------//

        // 深さ優先で積むので、親は必ず子より前に並ぶ（行列を前から順に計算できる）
        int AddSkeletonNode(const aiNode& node, int parentIndex, Skeleton& skeleton) {
            const int index = static_cast<int>(skeleton.nodes.size());

            BoneNode bone;
            bone.name = node.mName.C_Str();
            if (bone.name.empty()) {
                bone.name = "node_" + std::to_string(index);
            }
            bone.parentIndex = parentIndex;
            bone.localTransform = ToMatrix(node.mTransformation);
            skeleton.nameToIndex[bone.name] = index;
            skeleton.nodes.push_back(std::move(bone));

            for (unsigned int i = 0; i < node.mNumChildren; ++i) {
                const int child = AddSkeletonNode(*node.mChildren[i], index, skeleton);
                skeleton.nodes[index].children.push_back(child);
            }
            return index;
        }

        std::uint16_t GetOrAddBone(const aiBone& bone, SkinData& skin) {
            const std::string name = bone.mName.C_Str();
            if (const auto it = skin.boneNameToIndex.find(name); it != skin.boneNameToIndex.end()) {
                return it->second;
            }
            const auto index = static_cast<std::uint16_t>(skin.boneNames.size());
            skin.boneNames.push_back(name);
            skin.offsetMatrices.push_back(ToMatrix(bone.mOffsetMatrix));
            skin.boneNameToIndex.emplace(name, index);
            return index;
        }

        // 4 枠のうち空き（重み 0）に入れる。空きがなければ一番軽い枠と比べて、重いほうを残す
        void AddInfluence(SkinVertex& vertex, std::uint16_t bone, float weight) {
            int slot = -1;
            int lightest = 0;
            for (int i = 0; i < 4; ++i) {
                if (vertex.boneWeights[i] == 0.0f) {
                    slot = i;
                    break;
                }
                if (vertex.boneWeights[i] < vertex.boneWeights[lightest]) {
                    lightest = i;
                }
            }
            if (slot == -1) {
                if (weight <= vertex.boneWeights[lightest]) {
                    return;
                }
                slot = lightest;
            }
            vertex.boneIndices[slot] = bone;
            vertex.boneWeights[slot] = weight;
        }

        // 5 本以上から 4 本に削ったぶん合計が 1 を下回るので、残った 4 本で 1 に戻す
        void NormalizeWeights(std::vector<SkinVertex>& vertices) {
            for (SkinVertex& v : vertices) {
                const float sum = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
                if (sum <= 0.0f) {
                    continue;  // 0 で割らない
                }
                for (float& w : v.boneWeights) {
                    w /= sum;
                }
            }
        }

        // メッシュ番号 → そのメッシュを持っているノードの名前
        std::unordered_map<unsigned int, std::string> CollectMeshOwners(const aiNode& root) {
            std::unordered_map<unsigned int, std::string> owners;
            std::vector<const aiNode*> stack{ &root };
            while (!stack.empty()) {
                const aiNode* node = stack.back();
                stack.pop_back();
                for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
                    owners.emplace(node->mMeshes[i], node->mName.C_Str());
                }
                for (unsigned int i = 0; i < node->mNumChildren; ++i) {
                    stack.push_back(node->mChildren[i]);
                }
            }
            return owners;
        }

        // ---------------------------------------------------//
        //                    アニメーション                     //
        // ---------------------------------------------------//
        // assimp のチャンネルは、位置・回転・拡大縮小のキーの数と時刻がそれぞれ別。
        // 位置のキーの数で回すと、位置 1 個・回転 30 個のようなチャンネルで回転がほぼ捨てられる。
        // 3 つの時刻を全部集め、それぞれの時刻で 3 つとも補間し直す

        template <class TKey>
        unsigned int FindKey(const TKey* keys, unsigned int count, double t) {
            unsigned int k = 0;
            while (k + 1 < count && keys[k + 1].mTime <= t) {
                ++k;
            }
            return k;
        }

        aiVector3D SampleVector(const aiVectorKey* keys, unsigned int count, double t, const aiVector3D& fallback) {
            if (count == 0) {
                return fallback;
            }
            const unsigned int k = FindKey(keys, count, t);
            if (k + 1 >= count || t <= keys[k].mTime) {
                return keys[k].mValue;
            }
            const double span = keys[k + 1].mTime - keys[k].mTime;
            const float f = span > 1e-9 ? static_cast<float>((t - keys[k].mTime) / span) : 0.0f;
            return keys[k].mValue + (keys[k + 1].mValue - keys[k].mValue) * f;
        }

        aiQuaternion SampleQuaternion(const aiQuatKey* keys, unsigned int count, double t) {
            if (count == 0) {
                return aiQuaternion();
            }
            const unsigned int k = FindKey(keys, count, t);
            if (k + 1 >= count || t <= keys[k].mTime) {
                return keys[k].mValue;
            }
            const double span = keys[k + 1].mTime - keys[k].mTime;
            const float f = span > 1e-9 ? static_cast<float>((t - keys[k].mTime) / span) : 0.0f;
            aiQuaternion out;
            aiQuaternion::Interpolate(out, keys[k].mValue, keys[k + 1].mValue, f);
            return out.Normalize();
        }

        BoneAnimationChannel BuildChannel(const aiNodeAnim& source, double ticksPerSecond) {
            BoneAnimationChannel channel;
            channel.boneName = source.mNodeName.C_Str();

            std::vector<double> times;
            times.reserve(source.mNumPositionKeys + source.mNumRotationKeys + source.mNumScalingKeys);
            for (unsigned int k = 0; k < source.mNumPositionKeys; ++k) times.push_back(source.mPositionKeys[k].mTime);
            for (unsigned int k = 0; k < source.mNumRotationKeys; ++k) times.push_back(source.mRotationKeys[k].mTime);
            for (unsigned int k = 0; k < source.mNumScalingKeys; ++k)  times.push_back(source.mScalingKeys[k].mTime);
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end(),
                [](double a, double b) { return std::abs(a - b) < 1e-6; }),
                times.end());

            channel.keyFrames.reserve(times.size());
            for (const double t : times) {
                const aiVector3D p = SampleVector(source.mPositionKeys, source.mNumPositionKeys, t, aiVector3D(0.0f, 0.0f, 0.0f));
                const aiQuaternion q = SampleQuaternion(source.mRotationKeys, source.mNumRotationKeys, t);
                const aiVector3D s = SampleVector(source.mScalingKeys, source.mNumScalingKeys, t, aiVector3D(1.0f, 1.0f, 1.0f));
                KeyFrame key;
                key.time = static_cast<float>(t / ticksPerSecond);
                key.position = { p.x, p.y, p.z };
                key.rotation = { q.x, q.y, q.z, q.w };
                key.scale = { s.x, s.y, s.z };
                channel.keyFrames.push_back(key);
            }
            return channel;
        }

        // ---------------------------------------------------//
        //                      テクスチャ                       //
        // ---------------------------------------------------//

#if TE_PLATFORM_WINDOWS
        // デコード済みの画像を RGBA8 にそろえ、ミップマップを作って TextureData に詰める
        std::shared_ptr<TextureData> ToTextureData(const DirectX::ScratchImage& decoded) {
            const DirectX::Image* src = decoded.GetImage(0, 0, 0);
            if (src == nullptr) {
                return nullptr;
            }

            // Renderer は R8G8B8A8_UNORM だけを受け取る。形式の違いはここで吸収する。
            // BC 圧縮（DDS）は Convert では変換できないので、展開する
            DirectX::ScratchImage converted;
            if (DirectX::IsCompressed(src->format)) {
                if (FAILED(DirectX::Decompress(*src, DXGI_FORMAT_R8G8B8A8_UNORM, converted))) {
                    return nullptr;
                }
                src = converted.GetImage(0, 0, 0);
            }
            else if (src->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                if (FAILED(DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
                    return nullptr;
                }
                src = converted.GetImage(0, 0, 0);
            }

            // 縮小して表示したときのちらつき（エイリアシング）を防ぐ。levels = 0 で 1x1 まで全部作る。
            // 失敗しても元の 1 枚だけで描けるので、続ける
            DirectX::ScratchImage mipChain;
            const bool hasMips = SUCCEEDED(DirectX::GenerateMipMaps(*src, DirectX::TEX_FILTER_DEFAULT, 0, mipChain));
            const std::size_t levels = hasMips ? mipChain.GetMetadata().mipLevels : 1;

            auto out = std::make_shared<TextureData>();
            out->mips.reserve(levels);
            for (std::size_t level = 0; level < levels; ++level) {
                const DirectX::Image* image = hasMips ? mipChain.GetImage(level, 0, 0) : src;
                const std::size_t rowBytes = image->width * 4;
                const TextureData::Mip mip{ out->pixels.size(),
                                            static_cast<std::uint32_t>(image->width),
                                            static_cast<std::uint32_t>(image->height) };
                out->pixels.resize(out->pixels.size() + rowBytes * image->height);
                // DirectXTex の行の間隔（rowPitch）は行のバイト数より大きいことがあるので、1 行ずつ詰める
                for (std::size_t y = 0; y < image->height; ++y) {
                    std::memcpy(out->pixels.data() + mip.offset + rowBytes * y,
                        image->pixels + image->rowPitch * y, rowBytes);
                }
                out->mips.push_back(mip);
            }
            return out;
        }

        std::shared_ptr<TextureData> DecodeFile(const std::filesystem::path& path) {
            DirectX::ScratchImage image;
            const std::wstring ext = path.extension().wstring();
            HRESULT hr;
            if (_wcsicmp(ext.c_str(), L".tga") == 0) {
                hr = DirectX::LoadFromTGAFile(path.c_str(), nullptr, image);
            }
            else if (_wcsicmp(ext.c_str(), L".dds") == 0) {
                hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
            }
            else if (_wcsicmp(ext.c_str(), L".hdr") == 0) {
                hr = DirectX::LoadFromHDRFile(path.c_str(), nullptr, image);
            }
            else {
                hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);  // png / jpg / bmp
            }
            if (FAILED(hr)) {
                LogWarning("[ModelImporter] 画像のデコードに失敗: " + path.string());
                return nullptr;
            }
            return ToTextureData(image);
        }

        std::shared_ptr<TextureData> DecodeEmbedded(const aiTexture& texture) {
            DirectX::ScratchImage image;
            if (texture.mHeight == 0) {
                // png / jpg などのファイルの中身が丸ごと入っている（mWidth がバイト数）
                if (FAILED(DirectX::LoadFromWICMemory(reinterpret_cast<const std::uint8_t*>(texture.pcData), texture.mWidth,
                    DirectX::WIC_FLAGS_NONE, nullptr, image))) {
                    return nullptr;
                }
                return ToTextureData(image);
            }
            // 非圧縮の aiTexel。メモリ上は b, g, r, a の順なので、BGRA として受けて変換に任せる
            if (FAILED(image.Initialize2D(DXGI_FORMAT_B8G8R8A8_UNORM, texture.mWidth, texture.mHeight, 1, 1))) {
                return nullptr;
            }
            const DirectX::Image* dst = image.GetImage(0, 0, 0);
            const std::size_t rowBytes = static_cast<std::size_t>(texture.mWidth) * 4;
            for (unsigned int y = 0; y < texture.mHeight; ++y) {
                std::memcpy(dst->pixels + dst->rowPitch * y,
                    reinterpret_cast<const std::uint8_t*>(texture.pcData) + rowBytes * y, rowBytes);
            }
            return ToTextureData(image);
        }
#else
        // RGBA8 の画像に、2x2 の平均で 1x1 までのミップマップを足す
        std::shared_ptr<TextureData> ToTextureData(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height) {
            auto out = std::make_shared<TextureData>();
            out->pixels.assign(pixels, pixels + std::size_t{ width } * height * 4);
            out->mips.push_back(TextureData::Mip{ 0, width, height });

            std::uint32_t w = width;
            std::uint32_t h = height;
            while (w > 1 || h > 1) {
                const std::size_t srcOffset = out->mips.back().offset;
                const std::uint32_t nw = std::max(w / 2, 1u);
                const std::uint32_t nh = std::max(h / 2, 1u);
                const std::size_t dstOffset = out->pixels.size();
                out->pixels.resize(dstOffset + std::size_t{ nw } * nh * 4);
                const std::uint8_t* src = out->pixels.data() + srcOffset;
                std::uint8_t* dst = out->pixels.data() + dstOffset;
                for (std::uint32_t y = 0; y < nh; ++y) {
                    const std::uint32_t y0 = std::min(y * 2, h - 1);
                    const std::uint32_t y1 = std::min(y * 2 + 1, h - 1);
                    for (std::uint32_t x = 0; x < nw; ++x) {
                        const std::uint32_t x0 = std::min(x * 2, w - 1);
                        const std::uint32_t x1 = std::min(x * 2 + 1, w - 1);
                        for (std::uint32_t ch = 0; ch < 4; ++ch) {
                            const int sum = src[(std::size_t{ y0 } * w + x0) * 4 + ch] + src[(std::size_t{ y0 } * w + x1) * 4 + ch]
                                          + src[(std::size_t{ y1 } * w + x0) * 4 + ch] + src[(std::size_t{ y1 } * w + x1) * 4 + ch];
                            dst[(std::size_t{ y } * nw + x) * 4 + ch] = static_cast<std::uint8_t>((sum + 2) / 4);
                        }
                    }
                }
                out->mips.push_back(TextureData::Mip{ dstOffset, nw, nh });
                w = nw;
                h = nh;
            }
            return out;
        }

        std::shared_ptr<TextureData> DecodeFile(const std::filesystem::path& path) {
            int width = 0, height = 0, channels = 0;
            stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
            if (!pixels) {
                LogWarning("[ModelImporter] 画像のデコードに失敗: " + path.string());
                return nullptr;
            }
            auto out = ToTextureData(pixels, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
            stbi_image_free(pixels);
            return out;
        }

        std::shared_ptr<TextureData> DecodeEmbedded(const aiTexture& texture) {
            if (texture.mHeight == 0) {
                // png / jpg などのファイルの中身が丸ごと入っている（mWidth がバイト数）
                int width = 0, height = 0, channels = 0;
                stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(texture.pcData),
                    static_cast<int>(texture.mWidth), &width, &height, &channels, 4);
                if (!pixels) {
                    return nullptr;
                }
                auto out = ToTextureData(pixels, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
                stbi_image_free(pixels);
                return out;
            }
            // 非圧縮の aiTexel は b, g, r, a の順なので、r と b を入れ替える
            std::vector<std::uint8_t> rgba(std::size_t{ texture.mWidth } * texture.mHeight * 4);
            const auto* src = reinterpret_cast<const std::uint8_t*>(texture.pcData);
            for (std::size_t i = 0; i < rgba.size(); i += 4) {
                rgba[i + 0] = src[i + 2];
                rgba[i + 1] = src[i + 1];
                rgba[i + 2] = src[i + 0];
                rgba[i + 3] = src[i + 3];
            }
            return ToTextureData(rgba.data(), texture.mWidth, texture.mHeight);
        }
#endif

        // マテリアルが指すテクスチャ（ファイル名か、埋め込みの "*N"）をデコード済みの画像にする。
        // 同じ画像を何度もデコードしないよう、参照の文字列ごとに結果を覚えておく
        class TextureResolver {
        public:
            TextureResolver(const aiScene& scene, std::filesystem::path baseDir)
                : m_Scene(scene), m_BaseDir(std::move(baseDir)) {}

            std::shared_ptr<const TextureData> Resolve(const aiMaterial& material, aiTextureType type) {
                aiString reference;
                if (material.GetTexture(type, 0, &reference) != AI_SUCCESS || reference.length == 0) {
                    return nullptr;
                }
                const std::string key = reference.C_Str();
                if (const auto it = m_Cache.find(key); it != m_Cache.end()) {
                    return it->second;
                }

                std::shared_ptr<const TextureData> data;
                if (key[0] == '*') {
                    // glb / 埋め込み FBX。この版の assimp は "*番号" で mTextures を指す
                    const unsigned int index = static_cast<unsigned int>(std::atoi(key.c_str() + 1));
                    if (index < m_Scene.mNumTextures) {
                        data = DecodeEmbedded(*m_Scene.mTextures[index]);
                    }
                }
                else if (const std::filesystem::path file = FindFile(key); !file.empty()) {
                    data = DecodeFile(file);
                }
                else {
                    LogWarning("[ModelImporter] テクスチャが見つからない: " + key);
                }
                // 失敗も覚えておき、同じ失敗を繰り返さない
                m_Cache.emplace(key, data);
                return data;
            }

        private:
            // FBX には作った人の PC の絶対パスが入っていることが多い。
            // ファイル名だけ取り出して、モデルの横と textures/ フォルダを探す
            std::filesystem::path FindFile(const std::string& reference) const {/*
                std::filesystem::path name = FromUtf8(reference.c_str()).filename();*/
                std::string normalize = reference;
                std::replace(normalize.begin(), normalize.end(), '\\', '/');
                std::filesystem::path name = FromUtf8(normalize.c_str()).filename();
                if (name.extension() == ".exr") {
                    name.replace_extension(".png");  // exr はデコードできないので、同じ名前の png があればそれを使う
                }
                for (const auto& dir : { m_BaseDir / "textures", m_BaseDir }) {
                    std::error_code ec;
                    if (const auto candidate = dir / name; std::filesystem::exists(candidate, ec)) {
                        return candidate;
                    }
                }
                return {};
            }

            const aiScene& m_Scene;
            std::filesystem::path m_BaseDir;
            std::unordered_map<std::string, std::shared_ptr<const TextureData>> m_Cache;
        };

    }  // namespace

    std::optional<ModelCpuData> ImportModel(const std::filesystem::path& path, float scale) {
        // ConvertToLeftHanded : 右手系のデータを D3D の左手系にする（Z 反転・UV の上下反転・面の向きの反転をまとめて）。
        //                       これで面の表が時計回りになり、Renderer の「時計回りが表」と合う
        // JoinIdenticalVertices : 面ごとにばらばらの頂点を共有させる。頂点数が数分の 1 になる
        // LimitBoneWeights    : 1 頂点に効く骨を 4 本までにする（SkinVertex の枠の数）
        constexpr unsigned int kFlags =
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_LimitBoneWeights |
            aiProcess_ConvertToLeftHanded;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(ToUtf8(path), kFlags);
        if (scene == nullptr || !scene->HasMeshes() || scene->mRootNode == nullptr) {
            LogError("[ModelImporter] 読み込みに失敗: " + path.string() + " - " + importer.GetErrorString());
            return std::nullopt;
        }

        const bool hasBones = std::any_of(scene->mMeshes, scene->mMeshes + scene->mNumMeshes,
            [](const aiMesh* mesh) { return mesh->HasBones(); });

        // FBX は Z-up の補正がルートノードの回転に入っていることが多い。骨のないモデルはそれを頂点に焼いて立たせる。
        // 骨のあるモデルはスケルトン側に同じ回転が入るので焼かない（焼くと二重にかかって倒れる）。
        // 拡大縮小は焼かない。cm → m の 100 倍などが入っていて、焼くとモデルが巨大になる
        aiMatrix4x4 rootRotation;
        if (!hasBones) {
            aiVector3D rootScale;
            aiVector3D rootPosition;
            aiQuaternion rotation;
            scene->mRootNode->mTransformation.Decompose(rootScale, rotation, rootPosition);
            rootRotation = aiMatrix4x4(rotation.GetMatrix());
        }
        // 法線・接線は平行移動を含めず、逆転置で変換する
        aiMatrix3x3 normalMatrix(rootRotation);
        normalMatrix.Inverse().Transpose();

        // 骨のあるモデルに混じった骨なしのメッシュ（手に持つ剣・顔など）は、
        // 頂点が持ち主のノードの空間で入っているので、そのノードを骨として付ける
        const auto meshOwners = hasBones ? CollectMeshOwners(*scene->mRootNode)
            : std::unordered_map<unsigned int, std::string>{};

        ModelCpuData out;
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            const aiMesh& mesh = *scene->mMeshes[meshIndex];
            const auto baseVertex = static_cast<std::uint32_t>(out.vertices.size());

            // ---- 頂点 ---- //
            out.vertices.reserve(out.vertices.size() + mesh.mNumVertices);
            for (unsigned int i = 0; i < mesh.mNumVertices; ++i) {
                ModelVertex v;
                aiVector3D position = mesh.mVertices[i];
                position *= rootRotation;
                v.position = { position.x * scale, position.y * scale, position.z * scale };

                if (mesh.HasNormals()) {
                    const aiVector3D n = (normalMatrix * mesh.mNormals[i]).Normalize();
                    v.normal = { n.x, n.y, n.z };
                }
                else {
                    v.normal = Vector3::up;
                }
                if (mesh.HasTangentsAndBitangents()) {
                    const aiVector3D t = (normalMatrix * mesh.mTangents[i]).Normalize();
                    v.tangent = { t.x, t.y, t.z };
                }
                else {
                    v.tangent = Vector3::right;
                }
                if (mesh.HasTextureCoords(0)) {
                    v.uv = { mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y };
                }
                out.vertices.push_back(v);
            }

            // ---- スキン ---- //
            if (hasBones) {
                out.skinVertices.resize(out.vertices.size());
                for (unsigned int b = 0; b < mesh.mNumBones; ++b) {
                    const aiBone& bone = *mesh.mBones[b];
                    const std::uint16_t boneIndex = GetOrAddBone(bone, out.skin);
                    for (unsigned int w = 0; w < bone.mNumWeights; ++w) {
                        const aiVertexWeight& weight = bone.mWeights[w];
                        AddInfluence(out.skinVertices[baseVertex + weight.mVertexId], boneIndex, weight.mWeight);
                    }
                }
                // 骨なしのメッシュは、持ち主のノードを「重み 1 の骨」にする。
                // オフセット行列は単位行列（頂点がすでにノードの空間にあるので、ノードの行列を掛けるだけで置き場所に行く）。
                // 名前の表には入れない。同じノードが本物の骨でもあると、オフセット行列を取り違えるため
                if (mesh.mNumBones == 0) {
                    if (const auto owner = meshOwners.find(meshIndex); owner != meshOwners.end()) {
                        const auto boneIndex = static_cast<std::uint16_t>(out.skin.boneNames.size());
                        out.skin.boneNames.push_back(owner->second);
                        out.skin.offsetMatrices.push_back(Matrix4x4{});
                        for (unsigned int i = 0; i < mesh.mNumVertices; ++i) {
                            AddInfluence(out.skinVertices[baseVertex + i], boneIndex, 1.0f);
                        }
                    }
                }
            }

            // ---- インデックス ---- //
            const auto firstIndex = static_cast<std::uint32_t>(out.indices.size());
            out.indices.reserve(out.indices.size() + static_cast<std::size_t>(mesh.mNumFaces) * 3);
            for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
                const aiFace& face = mesh.mFaces[f];
                if (face.mNumIndices != 3) {
                    continue;  // Triangulate 後も残る点や線は描かない
                }
                out.indices.push_back(baseVertex + face.mIndices[0]);
                out.indices.push_back(baseVertex + face.mIndices[1]);
                out.indices.push_back(baseVertex + face.mIndices[2]);
            }
            const auto indexCount = static_cast<std::uint32_t>(out.indices.size()) - firstIndex;
            if (indexCount > 0) {
                out.subMeshes.push_back({ firstIndex, indexCount, mesh.mMaterialIndex });
            }
        }

        if (out.indices.empty()) {
            LogError("[ModelImporter] 三角形が 1 つもない: " + path.string());
            return std::nullopt;
        }
        NormalizeWeights(out.skinVertices);

        // ---- スケルトン ---- //
        AddSkeletonNode(*scene->mRootNode, -1, out.skeleton);
        // ルートの変換は ConvertToLeftHanded が頂点側にも入れている。ここでも持つと補正が二重になって裏返る
        // （assimp の globalInverseTransform を掛けるのと同じ意味）
        out.skeleton.nodes[0].localTransform = Matrix4x4{};

        // ---- マテリアル ---- //
        TextureResolver textures(*scene, path.parent_path());
        out.materials.resize(scene->mNumMaterials);
        for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
            const aiMaterial& source = *scene->mMaterials[i];
            ModelMaterial& dst = out.materials[i];

            aiString name;
            if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
                dst.name = name.C_Str();
            }
            // この版の assimp には BASE_COLOR がないので、glTF の baseColor も DIFFUSE に入る
            dst.baseColor = textures.Resolve(source, aiTextureType_DIFFUSE);
            dst.normal = textures.Resolve(source, aiTextureType_NORMALS);
            dst.emissive = textures.Resolve(source, aiTextureType_EMISSIVE);

            // 拡散色はテクスチャがないときだけ使う。テクスチャ付きの FBX は 0.8 などが入っていて、掛けると暗くなる
            aiColor4D diffuse(1.0f, 1.0f, 1.0f, 1.0f);
            if (!dst.baseColor && source.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
                dst.baseColorFactor = { diffuse.r, diffuse.g, diffuse.b, 1.0f };
            }
            aiColor3D emissive(0.0f, 0.0f, 0.0f);
            if (source.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
                dst.emissiveFactor = { emissive.r, emissive.g, emissive.b, 1.0f };
            }
        }

        // ---- アニメーション ---- //
        for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
            const aiAnimation& source = *scene->mAnimations[a];
            // 0 は「指定なし」。assimp の慣例に合わせて 25 にする
            const double ticksPerSecond = source.mTicksPerSecond != 0.0 ? source.mTicksPerSecond : 25.0;
            AnimationClip clip;
            clip.name = source.mName.C_Str();
            clip.duration = static_cast<float>(source.mDuration / ticksPerSecond);
            clip.channels.reserve(source.mNumChannels);
            for (unsigned int c = 0; c < source.mNumChannels; ++c) {
                clip.channels.push_back(BuildChannel(*source.mChannels[c], ticksPerSecond));
            }
            out.clips.push_back(std::move(clip));
        }

        LogInfo("[ModelImporter] " + path.filename().string() +
            " 頂点 " + std::to_string(out.vertices.size()) +
            " / 三角形 " + std::to_string(out.indices.size() / 3) +
            " / マテリアル " + std::to_string(out.materials.size()) +
            " / 骨 " + std::to_string(out.skin.boneNames.size()) +
            " / クリップ " + std::to_string(out.clips.size()));
        return out;
    }

}  // namespace TE