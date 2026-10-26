/**
 * \file Renderer.hpp
 * \brief RHI を使ってウィンドウに描画する
 */
#pragma once

#include "Math/Color.hpp"
#include "Math/Matrix4x4.hpp"
#include "Math/Vector3.hpp"
#include "Renderer/MeshRenderer.hpp"
#include "RHI/RHITypes.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

 // メッシュの頂点 1 個。色は物体ごとに MeshRenderer で決めるので、頂点には持たせない
struct MeshVertex {
    TE::Vector3 position;
    TE::Vector3 normal;
};

// RHI のインターフェースだけを使う。D3D12 / D3D11 の型はここには出てこない。
// ウィンドウも void* で受け取り、Platform（Win32）に依存しない。
//
// 1 フレームの流れ:
//   SetCamera → Draw を描きたい数だけ（ここでは積むだけ）→ Render（まとめて GPU へ送って Present）
class Renderer {
public:
    Renderer(ERHIBackend backend, void* windowHandle, std::uint32_t width, std::uint32_t height);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // バックバッファと深度バッファを作り直す。0 サイズ（最小化）は呼ばないこと
    void Resize(std::uint32_t width, std::uint32_t height);

    // ---- メッシュ ---- //
    // 頂点とインデックス（時計回りが表）を GPU に置き、番号を返す
    TE::MeshHandle CreateMesh(std::span<const MeshVertex> vertices, std::span<const std::uint16_t> indices);
    // 最初から用意してある 1 辺 1 の立方体
    TE::MeshHandle CubeMesh() const { return m_CubeMesh; }

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
        std::uint32_t indexCount = 0;
    };

    void CreateDepthBuffer(std::uint32_t width, std::uint32_t height);
    void CreatePipeline();
    void CreateConstantBuffer();
    void EnsureInstanceCapacity(std::size_t instanceCount);
    TE::MeshHandle CreateCubeMesh();

    // フレーム数ぶんのスロットを回す数。スワップチェインのバッファ数と揃える
    static constexpr std::uint32_t kFramesInFlight = 3;

    // 宣言の逆順に破棄されるので、デバイスを先頭に置く。
    // デバイスより長生きする RHI オブジェクトがあると、デバイスの破棄時に assert で止まる
    std::unique_ptr<IRHIDevice> m_Device;
    RHISwapChainRef m_SwapChain;
    RHICommandListRef m_CommandList;
    RHIFenceRef m_Fence;
    std::uint64_t m_FenceValue = 0;
    std::uint32_t m_FrameSlot = 0;

    // 深度バッファ。作った直後は Undefined なので、最初に使う前に DepthWrite へ遷移させる
    RHITextureRef m_DepthBuffer;
    bool m_DepthNeedsTransition = false;

    RHIPipelineStateRef m_Pipeline;

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

    std::vector<Mesh> m_Meshes;
    // メッシュごとに、このフレームで描くインスタンスを積む（添字 = MeshHandle::index）。
    // clear しても確保したメモリは残るので、2 フレーム目からはメモリ確保が起きない
    std::vector<std::vector<InstanceData>> m_PendingInstances;
    TE::MeshHandle m_CubeMesh;
};