/**
 * \file Renderer.hpp
 * \brief RHI を使ってウィンドウに描画する
 */
#pragma once

#include "RHI/RHITypes.hpp"

#include <cstdint>
#include <memory>

// RHI のインターフェースだけを使う。D3D12 / D3D11 の型はここには出てこない。
// ウィンドウも void* で受け取り、Platform（Win32）に依存しない。
class Renderer {
public:
    Renderer(ERHIBackend backend, void* windowHandle, std::uint32_t width, std::uint32_t height);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // バックバッファを作り直す。0 サイズ（最小化）は呼ばないこと
    void Resize(std::uint32_t width, std::uint32_t height);

    // 1 フレーム描いて Present する
    void Render();

private:
    void CreateTrianglePipeline();
    void CreateTriangleVertexBuffer();

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

    RHIPipelineStateRef m_TrianglePipeline;
    RHIBufferRef m_TriangleVertexBuffer;
};
