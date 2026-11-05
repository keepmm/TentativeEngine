/**
 * \file Renderer.hpp
 * \brief RHI を使ってウィンドウに描画する
 */
#pragma once

#include "Math/Bounds.hpp"
#include "Math/Color.hpp"
#include "Math/Matrix4x4.hpp"
#include "Math/Vector2.hpp"
#include "Math/Vector3.hpp"
#include "Renderer/MeshRenderer.hpp"
#include "RHI/RHITypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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

// 3D を描く先（ビュー）。それぞれが自分の RT・カメラ・大きさを持つ。
// Scene: エディターの Scene パネル（グリッドあり） / Game: Game パネル（機種の解像度で描く。グリッドなし）
enum class ViewId : std::uint8_t {
    Scene,
    Game,
    Count,
};

// RHI のインターフェースだけを使う。D3D12 / D3D11 の型はここには出てこない。
// ウィンドウも void* で受け取り、Platform（Win32）に依存しない。
//
// 1 フレームの流れ:
//   SetViewCamera（描くビューごと）→ Draw を描きたい数だけ（ここでは積むだけ）→ Render（まとめて GPU へ送って Present）
class ShaderWatcher;

class Renderer {
public:
    // shaderDirectory: Mesh.hlsl などがあるフォルダ。探すのは呼び出し側（Renderer は Win32 に依存しないため）
    Renderer(ERHIBackend backend, void* windowHandle, std::uint32_t width, std::uint32_t height,
             std::filesystem::path shaderDirectory);
    ~Renderer();

    // shaders フォルダを探すときの目印にするファイル
    static constexpr const char* kMeshShaderFile = "Mesh.hlsl";

    // 地面のグリッド。描く / 描かないを切り替える（既定は描く）。描くのは Scene ビューだけ
    void SetGridVisible(bool visible) { m_GridVisible = visible; }
    bool IsGridVisible() const { return m_GridVisible; }

    // UI のパス（バックバッファ、深度なし）の中で呼ばれる。3D は各ビューの RT に描き終えている。Renderer は中身を知らない
    struct OverlayContext {
        IRHICommandList& commandList;
        std::uint32_t frameSlot;   // このスロットを前回使った GPU の作業は終わっている
        ERHIFormat colorFormat;    // 今のパスのアタッチメント。PSO を合わせるのに使う
        ERHIFormat depthFormat;
    };
    using OverlayCallback = std::function<void(const OverlayContext&)>;
    void SetOverlayCallback(OverlayCallback callback) { m_OnOverlay = std::move(callback); }

    // RHI で自分のリソースを作るもの（ImGuiRenderer など）に貸す。Renderer より先に破棄すること
    IRHIDevice& Device() const { return *m_Device; }

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // バックバッファを作り直す（ビューの RT はパネルの大きさに合わせるので別）。0 サイズ（最小化）は呼ばないこと
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
    // 頂点をすべて囲む箱（メッシュの空間）。エディターでのクリック選択に使う。無効な番号なら nullptr
    const TE::Bounds* MeshBounds(TE::MeshHandle mesh) const {
        return mesh.IsValid() && mesh.index < m_Meshes.size() ? &m_Meshes[mesh.index].bounds : nullptr;
    }

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
    // 描くものを積む。全ビューで共通（同じインスタンスを、有効なビューの数だけ描く）。実際に GPU へ送るのは Render のとき
    void Draw(TE::MeshHandle mesh, const TE::Matrix4x4& world, const TE::Color& color);
    // 積んだものをまとめて描いて Present する
    void Render();

    // ---- ビュー（3D を描くオフスクリーンの RT）---- //
    // 3D はバックバッファではなく各ビューの RT に描き、バックバッファには UI だけを描く。
    // 同じ大きさなら何もしない。0 は無視する。RT を作り直したら ViewColor の中身が変わるので、取り直すこと
    void ResizeView(ViewId id, std::uint32_t width, std::uint32_t height);
    // false のビューは描かない（タブの裏に隠れているときなど）。状態は保たれるので、また true にすれば続きから描ける
    void SetViewEnabled(ViewId id, bool enabled) { GetView(id).enabled = enabled; }
    bool IsViewEnabled(ViewId id) const { return GetView(id).enabled; }
    // このビューを描くときのカメラと背景色。描く前に毎フレーム呼ぶ
    void SetViewCamera(ViewId id, const TE::Matrix4x4& view, const TE::Matrix4x4& projection, const TE::Color& clearColor);
    std::uint32_t ViewWidth(ViewId id) const { return GetView(id).width; }
    std::uint32_t ViewHeight(ViewId id) const { return GetView(id).height; }
    // 幅 / 高さ。RT がまだ無ければ 1
    float ViewAspect(ViewId id) const;
    // ImGui::Image で表示するためのテクスチャ。ResizeView で作り直されるので、変わったら取り直す。
    // まだ一度も ResizeView していないビューは空（nullptr）
    const RHITextureRef& ViewColor(ViewId id) const { return GetView(id).color; }

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
        TE::Bounds bounds;                   // 作るときに頂点から求める（CPU 側だけで使う）
    };

    struct Material {
        RHIDescriptorTableRef textures;  // t0: ベースカラー / t1: 法線 / t2: エミッシブ
        RHIBufferRef constants;          // b2: 係数。作るときに書いたら変えない
    };

    // 3D を描く先 1 つぶん
    struct ViewTarget {
        // 3D を描く先。色は UI のパスでシェーダーから読むので RenderTarget | ShaderResource
        RHITextureRef color;
        RHITextureRef depth;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        // 作り直した直後は色も深度も Undefined。最初に使う前だけ Undefined から遷移させる
        bool needsTransition = false;
        bool enabled = false;
        bool gridAllowed = false;  // グリッドを描いてよいビューか（Scene だけ）
        TE::Matrix4x4 view;
        TE::Matrix4x4 projection;
        TE::Color clearColor{ 0.10f, 0.12f, 0.18f, 1.0f };
        // フレームごとの定数（カメラ・ライト）。ビューごとに分ける（1 つを共有すると、2 つ目の書き込みで 1 つ目が上書きされる）。
        // Upload メモリなので、作ったときに一度 Map したまま書き続ける
        RHIBufferRef constants;
        void* mappedConstants = nullptr;
        // デバッグ名（リテラルを指すだけ。作り直すときにも使う）
        const char* colorName = "ViewColor";
        const char* depthName = "ViewDepth";
        const char* constantsName = "FrameConstants";
    };

    ViewTarget& GetView(ViewId id) { return m_Views[static_cast<std::size_t>(id)]; }
    const ViewTarget& GetView(ViewId id) const { return m_Views[static_cast<std::size_t>(id)]; }

    // ビューの色と深度を作り直す（大きさが変わったとき）
    void CreateViewTargets(ViewTarget& view, std::uint32_t width, std::uint32_t height);
    // ビューの定数バッファを作る
    void CreateConstantBuffer(ViewTarget& view);
    // 1 つのビューの 3D パスを記録する（定数の書き込み・バリア・描画・UI から読める状態への遷移まで）
    void RecordViewPass(ViewTarget& view);
    void CreatePipelineLayout();
    // VS / PS をファイルからコンパイルして PSO を作る。失敗したら nullptr（ログは出す）
    RHIPipelineStateRef BuildPipelineState();
    RHIPipelineStateRef BuildGridPipelineState();
    static constexpr const char* kGridShaderFile = "Grid.hlsl";
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

    // 添字 = ViewId。ビューの色と深度はここに持つ
    std::array<ViewTarget, static_cast<std::size_t>(ViewId::Count)> m_Views;

    // デスクリプタテーブルを作るときにレイアウトが要るので、PSO とは別に持っておく
    RHIPipelineLayoutRef m_PipelineLayout;
    RHIPipelineStateRef m_Pipeline;
    RHIPipelineStateRef m_GridPipeline;
    bool m_GridVisible = true;
    std::filesystem::path m_ShaderDirectory;
    // 前方宣言のまま unique_ptr で持てるのは、破棄する ~Renderer が .cpp（中身が見える所）にあるため
    std::unique_ptr<ShaderWatcher> m_ShaderWatcher;
    OverlayCallback m_OnOverlay;

    // 全メッシュぶんのインスタンスを 1 本に詰めるバッファ。足りなくなったら 2 倍に作り直す。全ビューで共有して読む
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
