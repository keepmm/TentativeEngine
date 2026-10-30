/**
 * \file Renderer.cpp
 * \brief RHI を使ってウィンドウに描画する
 */
#include "Renderer/Renderer.hpp"

#include "Core/Log.hpp"
#include "Math/Math.hpp"
#include "Renderer/ShaderCompiler.hpp"
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
        Vector4 lightDirection;  // xyz: 光が進む向き（ワールド空間）
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

    // 頂点は 2 本の頂点バッファから読む。
    //   スロット 0（頂点ごと）    : 位置・法線
    //   スロット 1（インスタンスごと）: ワールド行列・色。1 個描くごとに次の要素へ進む
    // これで同じメッシュを、行列だけ変えて 1 回の DrawIndexed でまとめて描ける（インスタンシング）
    // Vulkan（SPIR-V）のときだけ __spirv__ が定義されるので、Vulkan 用の注釈はそのときだけ付ける。
    //   VK_BINDING(b, s) : RHI の Vulkan バックエンドは「レイアウトのスロット番号 = set」「レジスタ番号 = binding」
    //   VK_LOCATION(n)   : 頂点属性は vertexAttributes に並べた順番が location になる
    //   ※ マクロの引数名を binding / location にすると、属性名の vk::binding まで置き換わって壊れる
    constexpr char kMeshShader[] = R"(
#ifdef __spirv__
#define VK_BINDING(b, s) [[vk::binding(b, s)]]
#define VK_LOCATION(n)   [[vk::location(n)]]
#else
#define VK_BINDING(b, s)
#define VK_LOCATION(n)
#endif

VK_BINDING(0, 0) cbuffer FrameConstants : register(b0)
{
    float4x4 g_ViewProjection;  // C++ 側で Transposed() してある（cbuffer は列優先で読むため）
    float4   g_LightDirection;
};

// マテリアル。スロット 2 なので Vulkan では set 2
VK_BINDING(2, 2) cbuffer MaterialConstants : register(b2)
{
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
};

// テクスチャはスロット 1 のテーブル（Vulkan では set 1、binding = レジスタ番号）。
// sRGB の画像（ベースカラー・エミッシブ）は、読んだ時点でリニアな値になっている
VK_BINDING(0, 1) Texture2D g_BaseColorMap : register(t0);
VK_BINDING(1, 1) Texture2D g_NormalMap    : register(t1);
VK_BINDING(2, 1) Texture2D g_EmissiveMap  : register(t2);
// 静的サンプラーは、Vulkan では set 0 の binding 100 + レジスタ番号に入る（RHI の決まり）
VK_BINDING(100, 0) SamplerState g_Sampler : register(s0);

struct VSInput
{
    VK_LOCATION(0) float3 position : POSITION;
    VK_LOCATION(1) float3 normal   : NORMAL;
    VK_LOCATION(2) float2 uv       : TEXCOORD0;
    VK_LOCATION(3) float3 tangent  : TANGENT;
    VK_LOCATION(4) float4 world0   : WORLD0;
    VK_LOCATION(5) float4 world1   : WORLD1;
    VK_LOCATION(6) float4 world2   : WORLD2;
    VK_LOCATION(7) float4 world3   : WORLD3;
    VK_LOCATION(8) float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 tangent  : TANGENT;
    float4 color    : COLOR;
};

PSInput VSMain(VSInput input)
{
    // 4 つの行を並べると、C++ の Matrix4x4（m[行][列]）と同じ並びの行列になる。転置はいらない
    const float4x4 world = float4x4(input.world0, input.world1, input.world2, input.world3);

    PSInput output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), world);
    output.position = mul(worldPosition, g_ViewProjection);
    // 向きなので w = 0。拡大率が軸ごとに違う物体を扱うときは、法線用の行列（NormalMatrix）に替える
    output.normal  = mul(float4(input.normal, 0.0f), world).xyz;
    output.tangent = mul(float4(input.tangent, 0.0f), world).xyz;
    output.uv      = input.uv;
    output.color   = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    // テクスチャは分岐の外で読む（分岐の中だと、ミップを選ぶための微分が正しく取れない）
    const float4 baseColorTex = g_BaseColorMap.Sample(g_Sampler, input.uv);
    const float3 normalTex    = g_NormalMap.Sample(g_Sampler, input.uv).xyz;
    const float3 emissiveTex  = g_EmissiveMap.Sample(g_Sampler, input.uv).rgb;

    // ---- 法線マップ ----
    // 補間すると法線と接線が直交しなくなるので、接線から法線方向の成分を取り除く（グラム・シュミット）
    float3 normal = normalize(input.normal);
    float3 tangent = input.tangent - normal * dot(input.tangent, normal);
    // 接線を持たないモデル（長さ 0）では、法線マップを使わず頂点の法線のままにする
    if (dot(tangent, tangent) > 1e-8f)
    {
        tangent = normalize(tangent);
        // 左手系なので cross(法線, 接線) が V の増える向き（画像の下向き）になる。DirectX 形式の法線マップに合う
        const float3 bitangent = cross(normal, tangent);
        // 画像の 0〜1 を -1〜1 に戻し、接空間（T, B, N）からワールド空間へ
        const float3 n = normalTex * 2.0f - 1.0f;
        normal = normalize(n.x * tangent + n.y * bitangent + n.z * normal);
    }

    // ---- 色 ----
    // ベースカラー = テクスチャ × マテリアルの係数 × 物体ごとの色（MeshRenderer）
    const float4 baseColor = baseColorTex * g_BaseColorFactor * input.color;
    const float3 emissive  = emissiveTex * g_EmissiveFactor.rgb;

    // ランバート反射: 面が光の来る方を向いているほど明るい。真っ暗にならないよう環境光を少し足す
    const float diffuse = saturate(dot(normal, -g_LightDirection.xyz));
    float3 color = baseColor.rgb * (0.25f + 0.75f * diffuse) + emissive;

    // ここまではリニアな値で計算した。スワップチェインは UNORM で sRGB 変換をしないので、
    // 画面に出す前にガンマ補正する（しないと暗く見える）
    color = pow(saturate(color), 1.0f / 2.2f);
    return float4(color, baseColor.a);
}
)";

    constexpr ERHIFormat kDepthFormat = ERHIFormat::D32_FLOAT;
    constexpr std::size_t kInitialInstanceCapacity = 1024;

}  // namespace

Renderer::Renderer(ERHIBackend backend, void* windowHandle, std::uint32_t width, std::uint32_t height) {
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

    CreateDepthBuffer(width, height);
    CreatePipeline();
    CreateConstantBuffer();
    EnsureInstanceCapacity(kInitialInstanceCapacity);
    // 立方体は既定のマテリアルを使うので、こちらを先に作る
    CreateDefaultMaterial();
    m_CubeMesh = CreateCubeMesh();

    // カメラが 1 台もないときでも何か写るよう、原点を少し離れて見ておく
    SetCamera(Matrix4x4::LookAtLH({ 0.0f, 1.0f, -3.0f }, Vector3::zero, Vector3::up),
        Matrix4x4::PerspectiveFovLH(60.0f, AspectRatio(), 0.1f, 1000.0f));
}

Renderer::~Renderer() {
    // GPU が使っている途中のリソースを解放しないよう、全部終わるのを待ってから手放す
    if (m_Device) {
        m_Device->WaitIdle();
    }
}

void Renderer::Resize(std::uint32_t width, std::uint32_t height) {
    // Resize の中で GPU の完了を待つので、ここで待つ必要はない
    if (!m_SwapChain->Resize(width, height)) {
        LogError("[Renderer] スワップチェインのリサイズに失敗");
        return;
    }
    // 深度バッファもバックバッファと同じ大きさでないと使えない
    CreateDepthBuffer(width, height);
}

float Renderer::AspectRatio() const {
    return static_cast<float>(m_SwapChain->GetWidth()) / static_cast<float>(m_SwapChain->GetHeight());
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

void Renderer::SetCamera(const Matrix4x4& view, const Matrix4x4& projection) {
    m_View = view;
    m_Projection = projection;
}

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
    // 前フレームの GPU の作業は Wait で終わっているので、ここで書き換えても描画中のデータは壊れない
    FrameConstants constants{};
    // 行ベクトルなので「ビュー → 投影」の順に左から掛ける
    constants.viewProjection = (m_View * m_Projection).Transposed();
    constants.lightDirection = Vector4::Direction(Vector3{ 0.4f, -1.0f, 0.6f }.Normalized());
    std::memcpy(m_MappedConstants, &constants, sizeof(constants));

    // 全メッシュのインスタンスを 1 本のバッファに詰める。メッシュごとに「何番目から何個」を覚えておく。
    // マテリアルの色はシェーダーで掛けるので、同じメッシュのサブメッシュは同じインスタンスを読み回せる
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
    // 前フレームの内容は使わないので Undefined から遷移させる
    m_CommandList->Transition(backBuffer, ERHIResourceState::Undefined, ERHIResourceState::RenderTarget);
    if (m_DepthNeedsTransition) {
        // 作り直した直後だけ。以降は DepthWrite のまま使い続ける
        m_CommandList->Transition(m_DepthBuffer.get(), ERHIResourceState::Undefined, ERHIResourceState::DepthWrite);
        m_DepthNeedsTransition = false;
    }

    RHIRenderPassDesc pass{};
    pass.numColors = 1;
    pass.colors[0].texture = backBuffer;
    pass.colors[0].loadOp = ERHILoadOp::Clear;
    pass.colors[0].storeOp = ERHIStoreOp::Store;
    pass.colors[0].clearColor[0] = m_ClearColor.r;
    pass.colors[0].clearColor[1] = m_ClearColor.g;
    pass.colors[0].clearColor[2] = m_ClearColor.b;
    pass.colors[0].clearColor[3] = m_ClearColor.a;
    // 深度は毎フレーム一番奥（1.0）で塗りつぶし、次のフレームには持ち越さない
    pass.depth.texture = m_DepthBuffer.get();
    pass.depth.loadOp = ERHILoadOp::Clear;
    pass.depth.storeOp = ERHIStoreOp::DontCare;
    pass.depth.clearDepth = 1.0f;
    m_CommandList->BeginRenderPass(pass);

    m_CommandList->SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
    m_CommandList->SetScissor({ 0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height) });
    m_CommandList->SetPipelineState(m_Pipeline.get());
    m_CommandList->SetConstantBuffer(kFrameConstantsSlot, m_ConstantBuffer.get());

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

// ---------------------------------------------------//
//                      作成                           //
// ---------------------------------------------------//

void Renderer::CreateDepthBuffer(std::uint32_t width, std::uint32_t height) {
    RHITextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = kDepthFormat;
    desc.usage = ERHITextureUsage::DepthStencil;  // シェーダーから読まないので、深度専用にして最適化を許す
    desc.clearValue.depth = 1.0f;                  // レンダーパスで塗る値と揃えると、クリアが速くなる
    desc.debugName = "DepthBuffer";
    // 古い深度バッファは手放すだけでよい。GPU が使い終わるまでは RHI の遅延解放で残る
    m_DepthBuffer = m_Device->CreateTexture(desc);
    if (!m_DepthBuffer) {
        throw std::runtime_error("深度バッファの生成に失敗しました");
    }
    m_DepthNeedsTransition = true;
}

void Renderer::CreatePipeline() {
    const std::string source = kMeshShader;
    // バックエンドに合わせて DXBC（D3D）か SPIR-V（Vulkan）を作る
    const ERHIBackend backend = m_Device->GetBackend();
    const auto vs = ShaderCompiler::Compile(source, "MeshVS", "VSMain", ShaderCompiler::Stage::Vertex, backend);
    const auto ps = ShaderCompiler::Compile(source, "MeshPS", "PSMain", ShaderCompiler::Stage::Pixel, backend);
    if (vs.empty() || ps.empty()) {
        throw std::runtime_error("メッシュのシェーダーのコンパイルに失敗しました");
    }

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
    psoDesc.rtvFormat[0] = m_SwapChain->GetFormat();
    psoDesc.debugName = "MeshPSO";

    m_Pipeline = m_Device->CreatePipelineState(psoDesc);
    if (!m_Pipeline) {
        throw std::runtime_error("PSO の生成に失敗しました");
    }
}

void Renderer::CreateConstantBuffer() {
    RHIBufferDesc desc{};
    desc.size = kConstantBufferSize;
    desc.usage = ERHIBufferUsage::ConstantBuffer;
    desc.memory = ERHIMemoryType::Upload;
    desc.debugName = "FrameConstants";
    m_ConstantBuffer = m_Device->CreateBuffer(desc);
    if (!m_ConstantBuffer) {
        throw std::runtime_error("定数バッファの生成に失敗しました");
    }
    // Upload は作ったときからずっとマップしておける。毎フレームこのポインタに書くだけでよい
    m_MappedConstants = m_ConstantBuffer->Map();
    if (m_MappedConstants == nullptr) {
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