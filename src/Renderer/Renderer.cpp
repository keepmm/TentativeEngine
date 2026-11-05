/**
 * \file Renderer.cpp
 * \brief RHI を使ってウィンドウに描画する
 */
#include "Renderer/Renderer.hpp"

#include "Core/Log.hpp"
#include "Math/Math.hpp"
#include "Renderer/ShaderCompiler.hpp"
#include "Renderer/ShaderWatcher.hpp"
#include "RHI/IRHIBuffer.hpp"
#include "RHI/IRHICommandList.hpp"
#include "RHI/IRHIDescriptorTable.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRHIFence.hpp"
#include "RHI/IRHIPipeline.hpp"
#include "RHI/IRHISwapChain.hpp"
#include "RHI/IRHITexture.hpp"
#include "RHI/RHIUtil.hpp"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

using namespace TE;

namespace {

    // GPU の cbuffer と同じ並びにする。HLSL の cbuffer は 16 バイト単位で詰めるので、
    // 大きさが 16 の倍数かを確かめておく（Vector3 を足すときは float 1 個ぶんの隙間を入れる）
    struct FrameConstants {
        Matrix4x4 viewProjection;
        Vector4 lightDirection;          // xyz: 光が進む向き（ワールド空間）
        Matrix4x4 inverseViewProjection; // 画面上の点をワールドに戻す（グリッドが使う）
        Vector4 cameraPosition;          // xyz: カメラのワールド位置
    };
    static_assert(sizeof(FrameConstants) % 16 == 0, "cbuffer の大きさは 16 バイトの倍数にする");
    static_assert(std::is_trivially_copyable_v<FrameConstants>, "GPU へは memcpy で送るので、ただのデータにする");

    // マテリアルごとの定数。シェーダーの cbuffer MaterialConstants と同じ並びにする
    struct MaterialConstants {
        Color baseColorFactor;
        Color emissiveFactor;
    };
    static_assert(sizeof(MaterialConstants) % 16 == 0, "cbuffer の大きさは 16 バイトの倍数にする");
    static_assert(std::is_trivially_copyable_v<MaterialConstants>, "GPU へは memcpy で送るので、ただのデータにする");

    // パイプラインレイアウトのスロット番号（コマンドリストで Set するときの番号）。
    // D3D11 は定数バッファのスロット番号をそのまま b# に使うので、定数バッファはスロット番号 = レジスタ番号にしておく
    constexpr std::uint32_t kFrameConstantsSlot = 0;     // b0
    constexpr std::uint32_t kMaterialTexturesSlot = 1;   // t0〜t2
    constexpr std::uint32_t kMaterialConstantsSlot = 2;  // b2

    // 定数バッファは 256 バイト単位（D3D12 の決まり）
    constexpr std::uint64_t kConstantBufferSize = 256;

    constexpr ERHIFormat kDepthFormat = ERHIFormat::D32_FLOAT;
    // 3D を描く RT（Scene / Game 共通）。バックバッファと同じく _SRGB にしない（シェーダーの出力をそのまま表示用の値として持つ）
    constexpr ERHIFormat kSceneColorFormat = ERHIFormat::R8G8B8A8_UNORM;
    constexpr std::size_t kInitialInstanceCapacity = 1024;

}  // namespace

Renderer::Renderer(ERHIBackend backend, void* windowHandle, std::uint32_t width, std::uint32_t height,
                   std::filesystem::path shaderDirectory)
    : m_ShaderDirectory(std::move(shaderDirectory)) {
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = backend;
    deviceDesc.frameInFlight = kFramesInFlight;
#ifdef _DEBUG
    // API の使い方の誤りを出力ウィンドウに出してもらう。重いので Debug ビルドだけ
    deviceDesc.enableDebugLayer = true;
#endif
    m_Device = CreateRHIDevice(deviceDesc);
    if (!m_Device) {
        throw std::runtime_error("RHI デバイスの生成に失敗しました");
    }
    LogInfo("[Renderer] アダプタ: " + m_Device->GetCaps().adapterName);

    RHISwapChainDesc swapChainDesc{};
    swapChainDesc.windowHandle = windowHandle;
    swapChainDesc.width = width;
    swapChainDesc.height = height;
    swapChainDesc.bufferCount = kFramesInFlight;
    swapChainDesc.format = ERHIFormat::R8G8B8A8_UNORM;
    swapChainDesc.vSync = true;
    m_SwapChain = m_Device->CreateSwapChain(swapChainDesc);
    if (!m_SwapChain) {
        throw std::runtime_error("スワップチェインの生成に失敗しました");
    }

    m_CommandList = m_Device->CreateCommandList("MainCommandList");
    m_UploadCommandList = m_Device->CreateCommandList("UploadCommandList");
    m_Fence = m_Device->CreateFence(0);
    if (!m_CommandList || !m_UploadCommandList || !m_Fence) {
        throw std::runtime_error("コマンドリスト / フェンスの生成に失敗しました");
    }

    // ビューごとの名前と性質。Scene だけグリッドを描く
    ViewTarget& scene = GetView(ViewId::Scene);
    scene.colorName = "SceneColor";
    scene.depthName = "SceneDepth";
    scene.constantsName = "SceneFrameConstants";
    scene.gridAllowed = true;
    scene.enabled = true;

    ViewTarget& game = GetView(ViewId::Game);
    game.colorName = "GameColor";
    game.depthName = "GameDepth";
    game.constantsName = "GameFrameConstants";
    game.gridAllowed = false;
    game.enabled = false;  // Game パネルが出るまでは描かない。RT もそのとき作る

    // Scene View の大きさは、パネルの大きさが分かるまでウィンドウと同じにしておく
    CreateViewTargets(scene, width, height);
    // レイアウトは作り直さない（マテリアルのデスクリプタテーブルがこれに結び付いているため）。
    // PSO はシェーダーを保存するたびに作り直す
    CreatePipelineLayout();
    m_Pipeline = BuildPipelineState();
    if (!m_Pipeline) {
        // 起動時は差し替える前の PSO が無いので、描けない。中身は出力ウィンドウのエラーを見る
        throw std::runtime_error("メッシュのパイプラインの生成に失敗しました");
    }
    // グリッドは無くても描けるので、失敗しても止めない
    m_GridPipeline = BuildGridPipelineState();
    m_ShaderWatcher = std::make_unique<ShaderWatcher>(m_ShaderDirectory);
    CreateConstantBuffer(scene);
    CreateConstantBuffer(game);
    EnsureInstanceCapacity(kInitialInstanceCapacity);
    // 立方体は既定のマテリアルを使うので、こちらを先に作る
    CreateDefaultMaterial();
    m_CubeMesh = CreateCubeMesh();

    // カメラが 1 台もないときでも何か写るよう、原点を少し離れて見ておく
    SetViewCamera(ViewId::Scene,
        Matrix4x4::LookAtLH({ 0.0f, 1.0f, -3.0f }, Vector3::zero, Vector3::up),
        Matrix4x4::PerspectiveFovLH(60.0f, ViewAspect(ViewId::Scene), 0.1f, 1000.0f),
        Color{ 0.10f, 0.12f, 0.18f, 1.0f });
}

Renderer::~Renderer() {
    // GPU が使っている途中のリソースを解放しないよう、全部終わるのを待ってから手放す
    if (m_Device) {
        m_Device->WaitIdle();
    }
}

void Renderer::Resize(std::uint32_t width, std::uint32_t height) {
    // Resize の中で GPU の完了を待つので、ここで待つ必要はない。
    // 深度はビューの大きさに合わせるので、ここでは作り直さない
    if (!m_SwapChain->Resize(width, height)) {
        LogError("[Renderer] スワップチェインのリサイズに失敗");
    }
}

void Renderer::ResizeView(ViewId id, std::uint32_t width, std::uint32_t height) {
    ViewTarget& view = GetView(id);
    if (width == 0 || height == 0 || (view.color && width == view.width && height == view.height)) {
        return;
    }
    // 前のフレームの GPU の作業は Render の最後の Wait で終わっている。古い RT は RHI の遅延解放で残る
    CreateViewTargets(view, width, height);
}

void Renderer::SetViewCamera(ViewId id, const Matrix4x4& view, const Matrix4x4& projection, const Color& clearColor) {
    ViewTarget& target = GetView(id);
    target.view = view;
    target.projection = projection;
    target.clearColor = clearColor;
}

float Renderer::ViewAspect(ViewId id) const {
    const ViewTarget& view = GetView(id);
    if (view.width == 0 || view.height == 0) {
        return 1.0f;
    }
    return static_cast<float>(view.width) / static_cast<float>(view.height);
}

void Renderer::SetPresentView(ViewId id)
{
    if (!m_PresentPipeline)
    {
        CreatePresentPipeline();
    }
    m_PresentView = id;
    m_PresentEnabled = true;
}

void Renderer::PollShaderReload(float deltaSeconds) {
    if (!m_ShaderWatcher->Poll(deltaSeconds)) {
        return;
    }
    // ShaderWatcher はどのファイルが変わったかを区別しないので、全部作り直す。
    // 失敗したものだけ古い PSO のまま描き続ける。直して保存すれば、もう一度ここに来る
    // 古い PSO は手放すだけでよい。GPU が使い終わるまでは RHI の遅延解放で残る
    bool reloaded = false;
    if (RHIPipelineStateRef pipeline = BuildPipelineState()) {
        m_Pipeline = std::move(pipeline);
        reloaded = true;
    }
    if (RHIPipelineStateRef grid = BuildGridPipelineState()) {
        m_GridPipeline = std::move(grid);
        reloaded = true;
    }
    if (reloaded) {
        LogInfo("[Renderer] シェーダーを読み直しました");
    }
}

// ---------------------------------------------------//
//                     メッシュ                        //
// ---------------------------------------------------//

MeshHandle Renderer::CreateMesh(std::span<const MeshVertex> vertices,
                                std::span<const std::uint32_t> indices,
                                std::span<const SubMeshDesc> subMeshes) {
    // 書き換えない小さなデータなので、GpuOnly への転送はせず Upload ヒープに置いたまま使う
    RHIBufferDesc vertexDesc{};
    vertexDesc.size = vertices.size_bytes();
    vertexDesc.usage = ERHIBufferUsage::VertexBuffer;
    vertexDesc.memory = ERHIMemoryType::Upload;
    vertexDesc.debugName = "MeshVB";

    RHIBufferDesc indexDesc{};
    indexDesc.size = indices.size_bytes();
    indexDesc.usage = ERHIBufferUsage::IndexBuffer;
    indexDesc.memory = ERHIMemoryType::Upload;
    indexDesc.debugName = "MeshIB";

    Mesh mesh;
    mesh.vertexBuffer = m_Device->CreateBuffer(vertexDesc, vertices.data());
    mesh.indexBuffer = m_Device->CreateBuffer(indexDesc, indices.data());
    if (!mesh.vertexBuffer || !mesh.indexBuffer) {
        throw std::runtime_error("メッシュの頂点 / インデックスバッファの生成に失敗しました");
    }

    // 区切りが無ければ全体を 1 つとして扱う。Render ではサブメッシュ単位で描くので、空にはしない
    if (subMeshes.empty()) {
        mesh.subMeshes.push_back({ 0, static_cast<std::uint32_t>(indices.size()), m_DefaultMaterial });
    } else {
        mesh.subMeshes.assign(subMeshes.begin(), subMeshes.end());
    }
    // 描くときに確かめなくて済むよう、使えないマテリアルはここで既定のものに差し替えておく
    for (SubMeshDesc& subMesh : mesh.subMeshes) {
        if (!subMesh.material.IsValid() || subMesh.material.index >= m_Materials.size()) {
            subMesh.material = m_DefaultMaterial;
        }
    }

    const MeshHandle handle{ static_cast<std::uint32_t>(m_Meshes.size()) };
    // 頂点をすべて囲む箱。GPU に送った後は頂点を読み返せないので、ここで求めておく
    if (!vertices.empty()) {
        mesh.bounds = Bounds{ vertices[0].position, Vector3{} };
        for (const MeshVertex& vertex : vertices) {
            mesh.bounds.Encapsulate(vertex.position);
        }
    }

    m_Meshes.push_back(std::move(mesh));
    m_PendingInstances.emplace_back();
    return handle;
}

MeshHandle Renderer::CreateCubeMesh() {
    // 面ごとに法線が違うので、角を共有せず 1 面 4 頂点 × 6 面で作る。
    // right と up は「その面を外から見たときの右と上」。左手系なので Cross(right, up) が内向き（-normal）になる
    struct Face {
        Vector3 normal;
        Vector3 right;
        Vector3 up;
    };
    const Face faces[] = {
        { Vector3::back,    Vector3::right,   Vector3::up },       // 手前（-Z）
        { Vector3::forward, Vector3::left,    Vector3::up },       // 奥（+Z）
        { Vector3::right,   Vector3::forward, Vector3::up },       // 右（+X）
        { Vector3::left,    Vector3::back,    Vector3::up },       // 左（-X）
        { Vector3::up,      Vector3::right,   Vector3::forward },  // 上（+Y）
        { Vector3::down,    Vector3::right,   Vector3::back },     // 下（-Y）
    };

    MeshVertex vertices[24];
    std::uint32_t indices[36];
    for (std::uint32_t f = 0; f < 6; ++f) {
        const Face& face = faces[f];
        const Vector3 center = face.normal * 0.5f;
        const Vector3 r = face.right * 0.5f;
        const Vector3 u = face.up * 0.5f;
        // 外から見て 左下 → 左上 → 右上 → 右下。UV は左上が (0, 0)（D3D の決まり）。
        // 接線は UV の u が増える向き = その面の右
        vertices[f * 4 + 0] = { center - r - u, face.normal, { 0.0f, 1.0f }, face.right };
        vertices[f * 4 + 1] = { center - r + u, face.normal, { 0.0f, 0.0f }, face.right };
        vertices[f * 4 + 2] = { center + r + u, face.normal, { 1.0f, 0.0f }, face.right };
        vertices[f * 4 + 3] = { center + r - u, face.normal, { 1.0f, 1.0f }, face.right };
        // 左下 → 左上 → 右上、左下 → 右上 → 右下。どちらも外から見て時計回り
        const std::uint32_t base = f * 4;
        const std::uint32_t quad[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; ++i) {
            indices[f * 6 + i] = base + quad[i];
        }
    }
    return CreateMesh(vertices, indices);
}

// ---------------------------------------------------//
//               テクスチャ / マテリアル                //
// ---------------------------------------------------//

TextureHandle Renderer::CreateTexture(std::span<const TextureMipDesc> mips, bool srgb, const char* debugName) {
    RHITextureRef texture = UploadTexture(mips, srgb, debugName != nullptr ? debugName : "Texture");
    if (!texture) {
        return {};  // 無効な番号を返す。マテリアルでは既定のテクスチャに置き換わる
    }
    const TextureHandle handle{ static_cast<std::uint32_t>(m_Textures.size()) };
    m_Textures.push_back(std::move(texture));
    return handle;
}

MaterialHandle Renderer::CreateMaterial(const MaterialDesc& desc) {
    Material material;

    // テクスチャの組。Vulkan では空いたスロットの中身が決まっていないので、使わないスロットにも必ず何か入れる
    material.textures = m_Device->CreateDescriptorTable(m_PipelineLayout.get(), kMaterialTexturesSlot);
    if (!material.textures) {
        throw std::runtime_error("マテリアルのデスクリプタテーブルの生成に失敗しました");
    }
    const TextureHandle handles[kMaterialTextureCount] = { desc.baseColor, desc.normal, desc.emissive };
    for (std::uint32_t i = 0; i < kMaterialTextureCount; ++i) {
        const TextureHandle handle = handles[i];
        const bool usable = handle.IsValid() && handle.index < m_Textures.size();
        material.textures->SetTexture(i, usable ? m_Textures[handle.index] : m_DefaultTextures[i]);
    }

    // 係数。作った後は書き換えないので、作るときに一度だけ書く。
    // CreateBuffer は size ぶん読むので、256 バイトの入れ物に詰めてから渡す
    MaterialConstants constants{ desc.baseColorFactor, desc.emissiveFactor };
    std::byte bytes[kConstantBufferSize]{};
    std::memcpy(bytes, &constants, sizeof(constants));

    RHIBufferDesc bufferDesc{};
    bufferDesc.size = kConstantBufferSize;
    bufferDesc.usage = ERHIBufferUsage::ConstantBuffer;
    bufferDesc.memory = ERHIMemoryType::Upload;
    bufferDesc.debugName = "MaterialConstants";
    material.constants = m_Device->CreateBuffer(bufferDesc, bytes);
    if (!material.constants) {
        throw std::runtime_error("マテリアルの定数バッファの生成に失敗しました");
    }

    const MaterialHandle handle{ static_cast<std::uint32_t>(m_Materials.size()) };
    m_Materials.push_back(std::move(material));
    return handle;
}

RHITextureRef Renderer::UploadTexture(std::span<const TextureMipDesc> mips, bool srgb, const char* debugName) {
    if (mips.empty() || mips[0].pixels == nullptr || mips[0].width == 0 || mips[0].height == 0) {
        LogError(std::string("[Renderer] 空の画像は GPU に置けません: ") + debugName);
        return nullptr;
    }

    RHITextureDesc desc{};
    desc.width = mips[0].width;
    desc.height = mips[0].height;
    desc.mipLevels = static_cast<std::uint32_t>(mips.size());
    // _SRGB にすると、シェーダーで読んだときにリニアな値へ直してくれる
    desc.format = srgb ? ERHIFormat::R8G8B8A8_UNORM_SRGB : ERHIFormat::R8G8B8A8_UNORM;
    desc.usage = ERHITextureUsage::ShaderResource;
    desc.debugName = debugName;
    RHITextureRef texture = m_Device->CreateTexture(desc);
    if (!texture) {
        throw std::runtime_error("テクスチャの生成に失敗しました");
    }

    // RGBA8 で隙間なく並んでいるので、1 行 = 幅 × 4 バイト
    std::vector<RHISubresourceData> subresources;
    subresources.reserve(mips.size());
    for (const TextureMipDesc& mip : mips) {
        const std::uint64_t rowPitch = static_cast<std::uint64_t>(mip.width) * 4;
        subresources.push_back({ mip.pixels, rowPitch, rowPitch * mip.height });
    }

    // 作った直後は Undefined。コピー先の状態にしてから送り、シェーダーで読む状態にする
    m_UploadCommandList->Begin();
    m_UploadCommandList->Transition(texture.get(), ERHIResourceState::Undefined, ERHIResourceState::CopyDst);
    // staging は GPU がコピーし終わるまで持っておく（下の Wait が終わるまで、この関数の中で生きている）
    const RHIBufferRef staging = RHIUploadTexture(*m_Device, *m_UploadCommandList, texture.get(),
        0, static_cast<std::uint32_t>(subresources.size()), subresources.data());
    m_UploadCommandList->Transition(texture.get(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResourcePS);
    m_UploadCommandList->End();
    if (!staging) {
        throw std::runtime_error("テクスチャのアップロードに失敗しました");
    }

    // 読み込みは起動時だけなので、まとめずにその場で送って待つ（いちばん単純で、寿命の間違いが起きない）
    IRHICommandList* lists[] = { m_UploadCommandList.get() };
    m_Device->Submit(lists, 1, m_Fence.get(), ++m_FenceValue);
    m_Fence->Wait(m_FenceValue);
    return texture;
}

void Renderer::CreatePresentPipeline()
{
    RHIPipelineLayoutDesc layoutDesc{};
    layoutDesc.slots = {
        { ERHIBindingType::ConstantBuffer, 0, 0, 1, ERHIShaderStage::Vertex },
        { ERHIBindingType::TextureTable, 0, 0, 1, ERHIShaderStage::Pixel },
    };
    // samplerの作成
    ERHIStaticSamplerDesc sampler{};
    sampler.shaderRegister = 0;
    sampler.filter = ERHIFilter::Linear;
    sampler.address = ERHIAddressMode::Clamp;
    sampler.visibility = ERHIShaderStage::Pixel;
    layoutDesc.staticSamplers = { sampler };
    layoutDesc.debugName = "PresentLayout";
    m_PresentLayout = m_Device->CreatePipelineLayout(layoutDesc);
    if (!m_PresentLayout) {
        throw std::runtime_error("転送用のパイプラインレイアウトの生成に失敗しました");
    }

    const std::filesystem::path path = m_ShaderDirectory / kPresentShaderFile;
    const ERHIBackend backend = m_Device->GetBackend();
    const auto vs = ShaderCompiler::CompileFile(path, "VSMain",
        ShaderCompiler::Stage::Vertex,backend);
    const auto ps = ShaderCompiler::CompileFile(path, "PSMain",
        ShaderCompiler::Stage::Pixel, backend);
    if (vs.empty() || ps.empty())
    {
        throw std::runtime_error("転送用のシェーダーを読めませんでした");
    }

    // pso作成
    RHIGraphicsPipelineDesc psoDesc{};
    psoDesc.layout = m_PresentLayout;
    psoDesc.vs = { vs.data(), vs.size(),"VSMain"};
    psoDesc.ps = { ps.data(), ps.size(),"PSMain" };
    psoDesc.rasterizer.cullMode = ERHICullMode::None;
    psoDesc.depthStencil.depthTestEnable = false;
    psoDesc.depthStencil.depthWriteEnable = false;
    psoDesc.numRenderTargets = 1;
    psoDesc.rtvFormat[0] = m_SwapChain->GetFormat();
    psoDesc.dsvFormat = ERHIFormat::Unknown;
    psoDesc.debugName = "PresentPSO";
    m_PresentPipeline = m_Device->CreatePipelineState(psoDesc);
    if (!m_PresentPipeline) {
        throw std::runtime_error("転送用の PSO の生成に失敗しました");
    }
}

void Renderer::RecordPresent()
{
    ViewTarget& view = GetView(m_PresentView);
    // カメラがなく描いてないフレームは
    // 中身が未定義なので読まない
    if (!view.enabled || !view.color)
    {
        return;
    }

    if (view.color.get() != m_PresentSource)
    {
        // 描画に使ったテーブルは書き換えれないので
        // 作り直す
        m_PresentTable = m_Device->CreateDescriptorTable(m_PresentLayout.get(), 1);
        if (!m_PresentTable)
        {
            throw std::runtime_error("転送用のデスクリプタテーブルの生成に失敗しました");
        }
        m_PresentTable->SetTexture(0, view.color);
        m_PresentSource = view.color.get();
    }

    m_CommandList->SetPipelineState(m_PresentPipeline.get());
    m_CommandList->SetConstantBuffer(0, view.constants.get());
    m_CommandList->SetDescriptorTable(1, m_PresentTable.get());
    m_CommandList->Draw(3);
}

void Renderer::CreateDefaultMaterial() {
    // テクスチャを持たないマテリアルが、テクスチャを持つものと同じシェーダーで描けるようにするための 1x1 の画像。
    // 掛け算・足し算をしても結果が変わらない値にしておく
    struct Solid {
        std::uint8_t rgba[4];
        bool srgb;
        const char* name;
    };
    const Solid solids[kMaterialTextureCount] = {
        { { 255, 255, 255, 255 }, true,  "DefaultBaseColor" },  // 白: 係数の色がそのまま出る
        { { 128, 128, 255, 255 }, false, "DefaultNormal" },     // (0, 0, 1): 曲げない法線
        { {   0,   0,   0, 255 }, true,  "DefaultEmissive" },   // 黒: 光らない
    };
    for (std::uint32_t i = 0; i < kMaterialTextureCount; ++i) {
        const TextureMipDesc mip{ solids[i].rgba, 1, 1 };
        m_DefaultTextures[i] = UploadTexture({ &mip, 1 }, solids[i].srgb, solids[i].name);
        if (!m_DefaultTextures[i]) {
            throw std::runtime_error("既定のテクスチャの生成に失敗しました");
        }
    }
    // テクスチャを何も指定しないと、全部が上の既定のテクスチャになる
    m_DefaultMaterial = CreateMaterial(MaterialDesc{});
}

// ---------------------------------------------------//
//                  1 フレームの描画                    //
// ---------------------------------------------------//

void Renderer::Draw(MeshHandle mesh, const Matrix4x4& world, const Color& color) {
    if (!mesh.IsValid() || mesh.index >= m_Meshes.size()) {
        return;  // まだメッシュを割り当てていない MeshRenderer は描かない
    }
    m_PendingInstances[mesh.index].push_back({ world, color });
}

void Renderer::Render() {
    // このスロットを前回使ったフレームは、前フレーム末の Wait で GPU が終えている
    m_Device->BeginFrame(m_FrameSlot);

    const std::uint32_t backBufferIndex = m_SwapChain->AcquireNextImage();
    IRHITexture* backBuffer = m_SwapChain->GetBackBuffer(backBufferIndex);
    const std::uint32_t width = m_SwapChain->GetWidth();
    const std::uint32_t height = m_SwapChain->GetHeight();

    // ---- GPU に送るデータを書く ---- //
    // 前フレームの GPU の作業は Wait で終わっているので、ここで書き換えても描画中のデータは壊れない。
    // 全メッシュのインスタンスを 1 本のバッファに詰める。メッシュごとに「何番目から何個」を覚えておく。
    // マテリアルの色はシェーダーで掛けるので、同じメッシュのサブメッシュは同じインスタンスを読み回せる。
    // 詰めるのは 1 回だけで、有効な全ビューが同じ範囲を読む（フレーム定数はビューごとに RecordViewPass で書く）
    std::size_t totalInstances = 0;
    for (const auto& instances : m_PendingInstances) {
        totalInstances += instances.size();
    }
    EnsureInstanceCapacity(totalInstances);
    std::size_t written = 0;
    for (const auto& instances : m_PendingInstances) {
        if (!instances.empty()) {
            std::memcpy(m_MappedInstances + written, instances.data(), instances.size() * sizeof(InstanceData));
            written += instances.size();
        }
    }

    // ---- 記録 ---- //
    m_CommandList->Begin();

    // ---- パス 1: 3D → 各ビューの RT ---- //
    // 有効なビューだけ描く。1 本のコマンドリストに続けて記録し、Submit は 1 回
    for (ViewTarget& view : m_Views) {
        if (view.enabled && view.color) {
            RecordViewPass(view);
        }
    }

    // ---- パス 2: UI → バックバッファ ---- //
    // 前フレームの内容は使わないので Undefined から遷移させる
    m_CommandList->Transition(backBuffer, ERHIResourceState::Undefined, ERHIResourceState::RenderTarget);

    RHIRenderPassDesc uiPass{};
    uiPass.numColors = 1;
    uiPass.colors[0].texture = backBuffer;
    uiPass.colors[0].loadOp = ERHILoadOp::Clear;
    uiPass.colors[0].storeOp = ERHIStoreOp::Store;
    m_CommandList->BeginRenderPass(uiPass);

    m_CommandList->SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
    m_CommandList->SetScissor({ 0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height) });
    if (m_PresentEnabled) RecordPresent();
    // 深度なしのパスなので depthFormat は Unknown（呼ばれた側はこれで PSO を選ぶ）
    if (m_OnOverlay) {
        m_OnOverlay({ *m_CommandList, m_FrameSlot, m_SwapChain->GetFormat(), ERHIFormat::Unknown });
    }

    m_CommandList->EndRenderPass();
    m_CommandList->Transition(backBuffer, ERHIResourceState::RenderTarget, ERHIResourceState::Present);
    m_CommandList->End();

    IRHICommandList* lists[] = { m_CommandList.get() };
    m_Device->Submit(lists, 1, m_Fence.get(), ++m_FenceValue);

    if (m_SwapChain->Present() == ERHIPresentResult::OutOfDate) {
        Resize(width, height);
    }

    // コマンドリストが 1 本しかないので、次のフレームで Begin し直す前に GPU の完了を待つ。
    // CPU と GPU を並行させたくなったら、コマンドリスト・定数バッファ・インスタンスバッファをスロットごとに持ってこの Wait を外す
    m_Fence->Wait(m_FenceValue);
    m_FrameSlot = (m_FrameSlot + 1) % kFramesInFlight;

    // 次のフレームのために空にする。clear は確保したメモリを残すので、次のフレームの push_back でメモリ確保が起きない
    for (auto& instances : m_PendingInstances) {
        instances.clear();
    }
}

void Renderer::RecordViewPass(ViewTarget& view) {
    // フレーム定数をこのビューの b0 に書く。
    // 前フレームの GPU の作業は Render の最後の Wait で終わっているので、ここで書き換えても描画中のデータは壊れない。
    // ビューごとに別のバッファなので、2 つ目のビューを書いても 1 つ目のパスが読む値は変わらない
    FrameConstants constants{};
    // 行ベクトルなので「ビュー → 投影」の順に左から掛ける
    const Matrix4x4 viewProjection = view.view * view.projection;
    constants.viewProjection = viewProjection.Transposed();
    constants.lightDirection = Vector4::Direction(Vector3{ 0.4f, -1.0f, 0.6f }.Normalized());
    constants.inverseViewProjection = viewProjection.Inverse().Transposed();
    // ビュー行列は「ワールド → カメラ」なので、逆行列は「カメラ → ワールド」。
    // 行ベクトルの行列では平行移動が 4 行目に入るので、そこがカメラのワールド位置になる
    const Matrix4x4 cameraToWorld = view.view.Inverse();
    constants.cameraPosition = Vector4{ cameraToWorld.m[3][0], cameraToWorld.m[3][1], cameraToWorld.m[3][2], 1.0f };
    std::memcpy(view.mappedConstants, &constants, sizeof(constants));

    if (view.needsTransition) {
        // 作り直した直後だけ。深度は以降 DepthWrite のまま使い続ける
        m_CommandList->Transition(view.color.get(), ERHIResourceState::Undefined, ERHIResourceState::RenderTarget);
        m_CommandList->Transition(view.depth.get(), ERHIResourceState::Undefined, ERHIResourceState::DepthWrite);
        view.needsTransition = false;
    } else {
        // 前に描いたフレームの最後に、UI から読める状態にしてあるので戻す
        m_CommandList->Transition(view.color.get(), ERHIResourceState::ShaderResourcePS, ERHIResourceState::RenderTarget);
    }

    RHIRenderPassDesc pass{};
    pass.numColors = 1;
    pass.colors[0].texture = view.color.get();
    pass.colors[0].loadOp = ERHILoadOp::Clear;
    pass.colors[0].storeOp = ERHIStoreOp::Store;  // UI のパスで読むので残す
    pass.colors[0].clearColor[0] = view.clearColor.r;
    pass.colors[0].clearColor[1] = view.clearColor.g;
    pass.colors[0].clearColor[2] = view.clearColor.b;
    pass.colors[0].clearColor[3] = view.clearColor.a;
    // 深度は毎フレーム一番奥（1.0）で塗りつぶし、次のフレームには持ち越さない
    pass.depth.texture = view.depth.get();
    pass.depth.loadOp = ERHILoadOp::Clear;
    pass.depth.storeOp = ERHIStoreOp::DontCare;
    pass.depth.clearDepth = 1.0f;
    m_CommandList->BeginRenderPass(pass);

    m_CommandList->SetViewport({ 0.0f, 0.0f, static_cast<float>(view.width), static_cast<float>(view.height), 0.0f, 1.0f });
    m_CommandList->SetScissor({ 0, 0, static_cast<std::int32_t>(view.width), static_cast<std::int32_t>(view.height) });
    m_CommandList->SetPipelineState(m_Pipeline.get());
    m_CommandList->SetConstantBuffer(kFrameConstantsSlot, view.constants.get());

    // サブメッシュごとに 1 回描く。同じメッシュの物体は何個あっても、サブメッシュ 1 つにつき DrawIndexed 1 回で済む
    std::size_t first = 0;
    for (std::size_t i = 0; i < m_Meshes.size(); ++i) {
        const std::uint32_t count = static_cast<std::uint32_t>(m_PendingInstances[i].size());
        if (count == 0) {
            continue;
        }
        const Mesh& mesh = m_Meshes[i];
        m_CommandList->SetVertexBuffer(0, mesh.vertexBuffer.get());
        m_CommandList->SetVertexBuffer(1, m_InstanceBuffer.get(), first * sizeof(InstanceData));
        m_CommandList->SetIndexBuffer(mesh.indexBuffer.get(), ERHIFormat::R32_UINT);
        for (const SubMeshDesc& subMesh : mesh.subMeshes) {
            // material は CreateMesh で有効なものに揃えてあるので、そのまま引ける
            const Material& material = m_Materials[subMesh.material.index];
            m_CommandList->SetDescriptorTable(kMaterialTexturesSlot, material.textures.get());
            m_CommandList->SetConstantBuffer(kMaterialConstantsSlot, material.constants.get());
            m_CommandList->DrawIndexed(subMesh.indexCount, count, subMesh.firstIndex);
        }
        first += count;
    }

    // グリッドは半透明なので、不透明なメッシュを描き終えてから重ねる（先に描くと、後から描いた物体に上書きされる）。
    // Scene ビューだけ描く（Game ビューにエディターの補助線は出さない）
    if (view.gridAllowed && m_GridVisible && m_GridPipeline) {
        m_CommandList->SetPipelineState(m_GridPipeline.get());
        // PSO を替えたので結び直す（RHI の決まり: バインドは PSO の後で）
        m_CommandList->SetConstantBuffer(kFrameConstantsSlot, view.constants.get());
        m_CommandList->Draw(6);  // 頂点バッファなし。VS が SV_VertexID から 2 枚の三角形を作る
    }

    m_CommandList->EndRenderPass();
    // UI のパスで ImGui::Image として読む
    m_CommandList->Transition(view.color.get(), ERHIResourceState::RenderTarget, ERHIResourceState::ShaderResourcePS);
}

// ---------------------------------------------------//
//                      作成                           //
// ---------------------------------------------------//

void Renderer::CreateViewTargets(ViewTarget& view, std::uint32_t width, std::uint32_t height) {
    RHITextureDesc colorDesc{};
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = kSceneColorFormat;
    colorDesc.usage = ERHITextureUsage::RenderTarget | ERHITextureUsage::ShaderResource;
    // レンダーパスで塗る値と揃えると、クリアが速くなる
    colorDesc.clearValue.color[0] = view.clearColor.r;
    colorDesc.clearValue.color[1] = view.clearColor.g;
    colorDesc.clearValue.color[2] = view.clearColor.b;
    colorDesc.clearValue.color[3] = view.clearColor.a;
    colorDesc.debugName = view.colorName;

    RHITextureDesc depthDesc{};
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = kDepthFormat;
    depthDesc.usage = ERHITextureUsage::DepthStencil;  // シェーダーから読まないので、深度専用にして最適化を許す
    depthDesc.clearValue.depth = 1.0f;
    depthDesc.debugName = view.depthName;

    // 古いものは手放すだけでよい。GPU が使い終わるまでは RHI の遅延解放で残る
    RHITextureRef color = m_Device->CreateTexture(colorDesc);
    RHITextureRef depth = m_Device->CreateTexture(depthDesc);
    if (!color || !depth) {
        throw std::runtime_error("ビューの RT / 深度バッファの生成に失敗しました");
    }
    view.color = std::move(color);
    view.depth = std::move(depth);
    view.width = width;
    view.height = height;
    view.needsTransition = true;
}

void Renderer::CreatePipelineLayout() {
    // 添字がスロット番号（kFrameConstantsSlot など）。D3D11 では定数バッファのスロット番号がそのままレジスタ番号になるので揃えておく
    RHIPipelineLayoutDesc layoutDesc{};
    layoutDesc.slots = {
        // b0: フレーム定数。ピクセルシェーダーもライトの向きを読むので、両方から見えるようにする
        { ERHIBindingType::ConstantBuffer, 0, 0, 1, ERHIShaderStage::All },
        // t0〜t2: マテリアルのテクスチャ（ベースカラー / 法線 / エミッシブ）
        { ERHIBindingType::TextureTable, 0, 0, kMaterialTextureCount, ERHIShaderStage::Pixel },
        // b2: マテリアルの係数
        { ERHIBindingType::ConstantBuffer, 2, 0, 1, ERHIShaderStage::Pixel },
    };
    // s0: 全テクスチャ共通。なめらかに補間し、UV が 0〜1 をはみ出したら繰り返す
    ERHIStaticSamplerDesc sampler{};
    sampler.shaderRegister = 0;
    sampler.filter = ERHIFilter::Linear;
    sampler.address = ERHIAddressMode::Wrap;
    sampler.visibility = ERHIShaderStage::Pixel;
    layoutDesc.staticSamplers = { sampler };
    layoutDesc.debugName = "MeshLayout";

    m_PipelineLayout = m_Device->CreatePipelineLayout(layoutDesc);
    if (!m_PipelineLayout) {
        throw std::runtime_error("パイプラインレイアウトの生成に失敗しました");
    }
}

RHIPipelineStateRef Renderer::BuildPipelineState() {
    // バックエンドに合わせて DXBC（D3D）か SPIR-V（Vulkan）を作る
    const std::filesystem::path path = m_ShaderDirectory / kMeshShaderFile;
    const ERHIBackend backend = m_Device->GetBackend();
    const auto vs = ShaderCompiler::CompileFile(path, "VSMain", ShaderCompiler::Stage::Vertex, backend);
    const auto ps = ShaderCompiler::CompileFile(path, "PSMain", ShaderCompiler::Stage::Pixel, backend);
    if (vs.empty() || ps.empty()) {
        return nullptr;  // エラーの中身は ShaderCompiler がログに出している
    }

    RHIGraphicsPipelineDesc psoDesc{};
    psoDesc.layout = m_PipelineLayout;
    psoDesc.vs = { vs.data(), vs.size(), "VSMain" };
    psoDesc.ps = { ps.data(), ps.size(), "PSMain" };
    const auto instanceOffset = [](std::size_t bytes) { return static_cast<std::uint32_t>(bytes); };
    psoDesc.vertexAttributes = {
        // スロット 0: 頂点ごと（並び順 = シェーダーの VK_LOCATION の番号）
        { "POSITION", 0, ERHIFormat::R32G32B32_FLOAT, 0, static_cast<std::uint32_t>(offsetof(MeshVertex, position)) },
        { "NORMAL",   0, ERHIFormat::R32G32B32_FLOAT, 0, static_cast<std::uint32_t>(offsetof(MeshVertex, normal)) },
        { "TEXCOORD", 0, ERHIFormat::R32G32_FLOAT,    0, static_cast<std::uint32_t>(offsetof(MeshVertex, uv)) },
        { "TANGENT",  0, ERHIFormat::R32G32B32_FLOAT, 0, static_cast<std::uint32_t>(offsetof(MeshVertex, tangent)) },
        // スロット 1: インスタンスごと。行列は 1 行（float4）ずつ 4 つの属性に分けて渡す
        { "WORLD", 0, ERHIFormat::R32G32B32A32_FLOAT, 1, instanceOffset(offsetof(InstanceData, world) + sizeof(float) * 4 * 0) },
        { "WORLD", 1, ERHIFormat::R32G32B32A32_FLOAT, 1, instanceOffset(offsetof(InstanceData, world) + sizeof(float) * 4 * 1) },
        { "WORLD", 2, ERHIFormat::R32G32B32A32_FLOAT, 1, instanceOffset(offsetof(InstanceData, world) + sizeof(float) * 4 * 2) },
        { "WORLD", 3, ERHIFormat::R32G32B32A32_FLOAT, 1, instanceOffset(offsetof(InstanceData, world) + sizeof(float) * 4 * 3) },
        { "COLOR", 0, ERHIFormat::R32G32B32A32_FLOAT, 1, instanceOffset(offsetof(InstanceData, color)) },
    };
    psoDesc.vertexBuffers = {
        { sizeof(MeshVertex), false },   // スロット 0: 頂点ごとに進む
        { sizeof(InstanceData), true },  // スロット 1: インスタンスごとに進む
    };
    // 時計回りに見える面が表（D3D の既定）。裏を向いた面は描かない
    psoDesc.rasterizer.cullMode = ERHICullMode::Back;
    psoDesc.depthStencil.depthTestEnable = true;
    psoDesc.depthStencil.depthWriteEnable = true;
    psoDesc.depthStencil.depthFunc = ERHICompareFunc::Less;  // 手前のものだけ残す
    psoDesc.dsvFormat = kDepthFormat;
    psoDesc.numRenderTargets = 1;
    psoDesc.rtvFormat[0] = kSceneColorFormat;  // 3D は各ビューの RT に描く（Scene / Game 共通のフォーマット）
    psoDesc.debugName = "MeshPSO";

    RHIPipelineStateRef pipeline = m_Device->CreatePipelineState(psoDesc);
    if (!pipeline) {
        LogError("[Renderer] PSO の生成に失敗しました");
    }
    return pipeline;
}

RHIPipelineStateRef Renderer::BuildGridPipelineState() {
    const std::filesystem::path path = m_ShaderDirectory / kGridShaderFile;
    const ERHIBackend backend = m_Device->GetBackend();
    const auto vs = ShaderCompiler::CompileFile(path, "VSMain", ShaderCompiler::Stage::Vertex, backend);
    const auto ps = ShaderCompiler::CompileFile(path, "PSMain", ShaderCompiler::Stage::Pixel, backend);
    if (vs.empty() || ps.empty()) {
        return nullptr;  // エラーの中身は ShaderCompiler がログに出している
    }

    RHIGraphicsPipelineDesc psoDesc{};
    // メッシュと同じレイアウトを使う。グリッドは b0 しか読まないが、レイアウトを分けるとスロットの番号も別に管理することになる
    psoDesc.layout = m_PipelineLayout;
    psoDesc.vs = { vs.data(), vs.size(), "VSMain" };
    psoDesc.ps = { ps.data(), ps.size(), "PSMain" };
    // vertexAttributes / vertexBuffers は空のまま（頂点バッファを使わない）
    // 地面の下から見上げたときも描けるよう、裏面も描く
    psoDesc.rasterizer.cullMode = ERHICullMode::None;
    // 物体より奥の格子は隠す（テストはする）。グリッドの後に描くものを邪魔しないよう、深度は書かない
    psoDesc.depthStencil.depthTestEnable = true;
    psoDesc.depthStencil.depthWriteEnable = false;
    psoDesc.depthStencil.depthFunc = ERHICompareFunc::LessEqual;
    // アルファで背景と混ぜる（線の端をなめらかにし、遠くを薄くするため）
    psoDesc.blend[0] = RHIBlendDesc::AlphaBlend();
    psoDesc.dsvFormat = kDepthFormat;
    psoDesc.numRenderTargets = 1;
    psoDesc.rtvFormat[0] = kSceneColorFormat;  // 3D は各ビューの RT に描く（Scene / Game 共通のフォーマット）
    psoDesc.debugName = "GridPSO";

    RHIPipelineStateRef pipeline = m_Device->CreatePipelineState(psoDesc);
    if (!pipeline) {
        LogError("[Renderer] グリッドの PSO の生成に失敗しました");
    }
    return pipeline;
}

void Renderer::CreateConstantBuffer(ViewTarget& view) {
    RHIBufferDesc desc{};
    desc.size = kConstantBufferSize;
    desc.usage = ERHIBufferUsage::ConstantBuffer;
    desc.memory = ERHIMemoryType::Upload;
    desc.debugName = view.constantsName;
    view.constants = m_Device->CreateBuffer(desc);
    if (!view.constants) {
        throw std::runtime_error("定数バッファの生成に失敗しました");
    }
    // Upload は作ったときからずっとマップしておける。毎フレームこのポインタに書くだけでよい
    view.mappedConstants = view.constants->Map();
    if (view.mappedConstants == nullptr) {
        throw std::runtime_error("定数バッファのマップに失敗しました");
    }
}

void Renderer::EnsureInstanceCapacity(std::size_t instanceCount) {
    if (instanceCount <= m_InstanceCapacity) {
        return;
    }
    // 毎フレーム作り直さないよう、足りなくなったら 2 倍ずつ広げる
    std::size_t capacity = m_InstanceCapacity == 0 ? kInitialInstanceCapacity : m_InstanceCapacity;
    while (capacity < instanceCount) {
        capacity *= 2;
    }

    RHIBufferDesc desc{};
    desc.size = capacity * sizeof(InstanceData);
    desc.usage = ERHIBufferUsage::VertexBuffer;
    desc.memory = ERHIMemoryType::Upload;
    desc.debugName = "InstanceBuffer";
    // 古いバッファは手放すだけでよい。GPU が使い終わるまでは RHI の遅延解放で残る
    m_InstanceBuffer = m_Device->CreateBuffer(desc);
    if (!m_InstanceBuffer) {
        throw std::runtime_error("インスタンスバッファの生成に失敗しました");
    }
    m_MappedInstances = static_cast<InstanceData*>(m_InstanceBuffer->Map());
    if (m_MappedInstances == nullptr) {
        throw std::runtime_error("インスタンスバッファのマップに失敗しました");
    }
    m_InstanceCapacity = capacity;
}
