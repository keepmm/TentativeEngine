/**
 * \file Renderer.hpp
 * \brief RHI を使ってウィンドウに描画する
 */
#pragma once

#include "Math/Color.hpp"
#include "Math/Matrix4x4.hpp"
#include "Math/Vector2.hpp"
#include "Math/Vector3.hpp"
#include "Renderer/MeshRenderer.hpp"
#include "RHI/RHITypes.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

// メッシュの頂点 1 個。色は物体ごとに MeshRenderer で決めるので、頂点には持たせない。
// 並びは TE::ModelVertex と同じにしておく（読み込んだモデルをそのまま写せるように）
struct MeshVertex {
    TE::Vector3 position;
    TE::Vector3 normal;
    TE::Vector2 uv;
    TE::Vector3 tangent;  // 法線マップ用。今はシェーダーで使っていない
};

// メッシュの中の「ここからここまでを、このマテリアルで描く」という区切り
struct SubMeshDesc {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    TE::MaterialHandle material;  // 無効なら既定のマテリアル（白）
};

// テクスチャ 1 段ぶん。RGBA8 で、行の間に隙間なく並んでいること
struct TextureMipDesc {
    const void* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// マテリアルの作り方。テクスチャが無効なら既定のテクスチャ（白 / まっすぐな法線 / 黒）を使う
struct MaterialDesc {
    TE::TextureHandle baseColor;
    TE::TextureHandle normal;
    TE::TextureHandle emissive;
    TE::Color baseColorFactor = TE::Color::white;  // テクスチャの色に掛ける
    TE::Color emissiveFactor = TE::Color::black;   // エミッシブのテクスチャに掛ける
};

// RHI のインターフェースだけを使う。D3D12 / D3D11 の型はここには出てこない。
// ウィンドウも void* で受け取り、Platform（Win32）に依存しない。
//
// 1 フレームの流れ:
//   SetCamera → Draw を描きたい数だけ（ここでは積むだけ）→ Render（まとめて GPU へ送って Present）
class ShaderWatcher;

class Renderer {
public:
    // shaderDirectory: Mesh.hlsl などがあるフォルダ。探すのは呼び出し側（Renderer は Win32 に依存しないため）
    Renderer(ERHIBackend backend, void* windowHandle, std::uint32_t width, std::uint32_t height,
             std::filesystem::path shaderDirectory);
    ~Renderer();

    // shaders フォルダを探すときの目印にするファイル
    static constexpr const char* kMeshShaderFile = "Mesh.hlsl";

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // バックバッファと深度バッファを作り直す。0 サイズ（最小化）は呼ばないこと
    void Resize(std::uint32_t width, std::uint32_t height);

    // シェーダーのファイルが保存されていたら作り直す。毎フレーム呼んでよい（確認は 0.5 秒おき）
    void PollShaderReload(float deltaSeconds);

    // ---- メッシュ ---- //
    // 頂点とインデックス（時計回りが表）を GPU に置き、番号を返す。
    // subMeshes を省くと、全体を 1 つのサブメッシュ（白）として扱う
    TE::MeshHandle CreateMesh(std::span<const MeshVertex> vertices,
                              std::span<const std::uint32_t> indices,
                              std::span<const SubMeshDesc> subMeshes = {});
    // 最初から用意してある 1 辺 1 の立方体
    TE::MeshHandle CubeMesh() const { return m_CubeMesh; }

    // ---- テクスチャ / マテリアル ---- //
    // RGBA8 の画像を GPU に置き、番号を返す。mips[0] が一番大きい段。
    // srgb = true はベースカラーやエミッシブのような「色」の画像。法線マップのような「数値」の画像は false
    // 送り終わるまで待つので、読み込み時に呼ぶこと（毎フレーム呼ぶものではない）
    TE::TextureHandle CreateTexture(std::span<const TextureMipDesc> mips, bool srgb, const char* debugName = nullptr);
    // テクスチャと係数をまとめて、描くときに 1 回で結び付けられる形にする。作った後は変えられない
    TE::MaterialHandle CreateMaterial(const MaterialDesc& desc);
    // 白い、テクスチャなしのマテリアル
    TE::MaterialHandle DefaultMaterial() const { return m_DefaultMaterial; }

    // ---- 1 フレームの描画 ---- //
    void SetCamera(const TE::Matrix4x4& view, const TE::Matrix4x4& projection);
    void SetClearColor(const TE::Color& color) { m_ClearColor = color; }
    // 描くものを積む。実際に GPU へ送るのは Render のとき
    void Draw(TE::MeshHandle mesh, const TE::Matrix4x4& world, const TE::Color& color);
    // 積んだものをまとめて描いて Present する
    void Render();

    float AspectRatio() const;

private:
    // 物体 1 個ぶんのデータ。インスタンス用の頂点バッファにこのまま詰めて送る
    struct InstanceData {
        TE::Matrix4x4 world;  // 行ベクトルの行列。シェーダーでは 4 つの行（WORLD0〜3）として受け取る
        TE::Color color;
    };

    struct Mesh {
        RHIBufferRef vertexBuffer;
        RHIBufferRef indexBuffer;
        std::vector<SubMeshDesc> subMeshes;  // 少なくとも 1 つある。material は必ず有効
    };

    struct Material {
        RHIDescriptorTableRef textures;  // t0: ベースカラー / t1: 法線 / t2: エミッシブ
        RHIBufferRef constants;          // b2: 係数。作るときに書いたら変えない
    };

    void CreateDepthBuffer(std::uint32_t width, std::uint32_t height);
    void CreatePipelineLayout();
    // VS / PS をファイルからコンパイルして PSO を作る。失敗したら nullptr（ログは出す）
    RHIPipelineStateRef BuildPipelineState();
    void CreateConstantBuffer();
    void CreateDefaultMaterial();
    void EnsureInstanceCapacity(std::size_t instanceCount);
    TE::MeshHandle CreateCubeMesh();
    // 画像を GPU に送って、送り終わるまで待つ
    RHITextureRef UploadTexture(std::span<const TextureMipDesc> mips, bool srgb, const char* debugName);

    // フレーム数ぶんのスロットを回す数。スワップチェインのバッファ数と揃える
    static constexpr std::uint32_t kFramesInFlight = 3;
    // マテリアル 1 つが持つテクスチャの枚数（シェーダーの t0〜t2）。増やすときはシェーダーと既定のテクスチャも揃える
    static constexpr std::uint32_t kMaterialTextureCount = 3;

    // 宣言の逆順に破棄されるので、デバイスを先頭に置く。
    // デバイスより長生きする RHI オブジェクトがあると、デバイスの破棄時に assert で止まる
    std::unique_ptr<IRHIDevice> m_Device;
    RHISwapChainRef m_SwapChain;
    RHICommandListRef m_CommandList;
    // テクスチャのアップロード用。毎フレームの記録とは別に、読み込みのときだけ使う
    RHICommandListRef m_UploadCommandList;
    RHIFenceRef m_Fence;
    std::uint64_t m_FenceValue = 0;
    std::uint32_t m_FrameSlot = 0;

    // 深度バッファ。作った直後は Undefined なので、最初に使う前に DepthWrite へ遷移させる
    RHITextureRef m_DepthBuffer;
    bool m_DepthNeedsTransition = false;

    // デスクリプタテーブルを作るときにレイアウトが要るので、PSO とは別に持っておく
    RHIPipelineLayoutRef m_PipelineLayout;
    RHIPipelineStateRef m_Pipeline;
    std::filesystem::path m_ShaderDirectory;
    // 前方宣言のまま unique_ptr で持てるのは、破棄する ~Renderer が .cpp（中身が見える所）にあるため
    std::unique_ptr<ShaderWatcher> m_ShaderWatcher;

    // フレームごとの定数（カメラ・ライト）。Upload メモリなので、作ったときに一度 Map したまま書き続ける
    RHIBufferRef m_ConstantBuffer;
    void* m_MappedConstants = nullptr;
    TE::Matrix4x4 m_View;
    TE::Matrix4x4 m_Projection;
    TE::Color m_ClearColor{ 0.10f, 0.12f, 0.18f, 1.0f };

    // 全メッシュぶんのインスタンスを 1 本に詰めるバッファ。足りなくなったら 2 倍に作り直す
    RHIBufferRef m_InstanceBuffer;
    InstanceData* m_MappedInstances = nullptr;
    std::size_t m_InstanceCapacity = 0;

    std::vector<RHITextureRef> m_Textures;  // 添字 = TextureHandle::index
    std::vector<Material> m_Materials;      // 添字 = MaterialHandle::index
    // テクスチャが無いスロットに入れる 1x1 の画像。並びは Material::textures と同じ
    RHITextureRef m_DefaultTextures[kMaterialTextureCount];
    TE::MaterialHandle m_DefaultMaterial;

    std::vector<Mesh> m_Meshes;
    // メッシュごとに、このフレームで描くインスタンスを積む（添字 = MeshHandle::index）。
    // clear しても確保したメモリは残るので、2 フレーム目からはメモリ確保が起きない
    std::vector<std::vector<InstanceData>> m_PendingInstances;
    TE::MeshHandle m_CubeMesh;
};