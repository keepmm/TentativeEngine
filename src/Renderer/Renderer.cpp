/**
 * \file Renderer.cpp
 * \brief RHI を使ってウィンドウに描画する
 */
#include "Renderer/Renderer.hpp"

#include "Core/Log.hpp"
#include "Renderer/ShaderCompiler.hpp"
#include "RHI/IRHIBuffer.hpp"
#include "RHI/IRHICommandList.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRHIFence.hpp"
#include "RHI/IRHIPipeline.hpp"
#include "RHI/IRHISwapChain.hpp"
#include "RHI/IRHITexture.hpp"

#include <cstddef>
#include <stdexcept>

namespace {

struct TriangleVertex {
    float position[3];
    float color[4];
};

// 頂点はクリップ空間の座標をそのまま渡す。カメラや行列はまだ持たない
constexpr char kTriangleShader[] = R"(
struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct PSInput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 1.0f);
    output.color    = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    return input.color;
}
)";

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
    m_Fence = m_Device->CreateFence(0);
    if (!m_CommandList || !m_Fence) {
        throw std::runtime_error("コマンドリスト / フェンスの生成に失敗しました");
    }

    CreateTrianglePipeline();
    CreateTriangleVertexBuffer();
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
    }
}

void Renderer::Render() {
    // このスロットを前回使ったフレームは、前フレーム末の Wait で GPU が終えている
    m_Device->BeginFrame(m_FrameSlot);

    const std::uint32_t backBufferIndex = m_SwapChain->AcquireNextImage();
    IRHITexture* backBuffer = m_SwapChain->GetBackBuffer(backBufferIndex);
    const std::uint32_t width = m_SwapChain->GetWidth();
    const std::uint32_t height = m_SwapChain->GetHeight();

    m_CommandList->Begin();
    // 前フレームの内容は使わないので Undefined から遷移させる
    m_CommandList->Transition(backBuffer, ERHIResourceState::Undefined, ERHIResourceState::RenderTarget);

    RHIRenderPassDesc pass{};
    pass.numColors = 1;
    pass.colors[0].texture = backBuffer;
    pass.colors[0].loadOp = ERHILoadOp::Clear;
    pass.colors[0].storeOp = ERHIStoreOp::Store;
    pass.colors[0].clearColor[0] = 0.10f;
    pass.colors[0].clearColor[1] = 0.12f;
    pass.colors[0].clearColor[2] = 0.18f;
    pass.colors[0].clearColor[3] = 1.0f;
    m_CommandList->BeginRenderPass(pass);

    m_CommandList->SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
    m_CommandList->SetScissor({ 0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height) });

    m_CommandList->SetPipelineState(m_TrianglePipeline.get());
    m_CommandList->SetVertexBuffer(0, m_TriangleVertexBuffer.get());
    m_CommandList->Draw(3);

    m_CommandList->EndRenderPass();
    m_CommandList->Transition(backBuffer, ERHIResourceState::RenderTarget, ERHIResourceState::Present);
    m_CommandList->End();

    IRHICommandList* lists[] = { m_CommandList.get() };
    m_Device->Submit(lists, 1, m_Fence.get(), ++m_FenceValue);

    if (m_SwapChain->Present() == ERHIPresentResult::OutOfDate) {
        m_SwapChain->Resize(width, height);
    }

    // コマンドリストが 1 本しかないので、次のフレームで Begin し直す前に GPU の完了を待つ。
    // CPU と GPU を並行させたくなったら、コマンドリストをスロットごとに持ってこの Wait を外す
    m_Fence->Wait(m_FenceValue);
    m_FrameSlot = (m_FrameSlot + 1) % kFramesInFlight;
}

void Renderer::CreateTrianglePipeline() {
    const std::string source = kTriangleShader;
    const auto vs = ShaderCompiler::Compile(source, "TriangleVS", "VSMain", "vs_5_0");
    const auto ps = ShaderCompiler::Compile(source, "TrianglePS", "PSMain", "ps_5_0");
    if (vs.empty() || ps.empty()) {
        throw std::runtime_error("三角形のシェーダーのコンパイルに失敗しました");
    }

    // バインドするリソースがないので、スロットは空のレイアウトでよい
    RHIPipelineLayoutDesc layoutDesc{};
    layoutDesc.debugName = "TriangleLayout";

    RHIGraphicsPipelineDesc psoDesc{};
    psoDesc.layout = m_Device->CreatePipelineLayout(layoutDesc);
    if (!psoDesc.layout) {
        throw std::runtime_error("パイプラインレイアウトの生成に失敗しました");
    }
    psoDesc.vs = { vs.data(), vs.size(), "VSMain" };
    psoDesc.ps = { ps.data(), ps.size(), "PSMain" };
    psoDesc.vertexAttributes = {
        { "POSITION", 0, ERHIFormat::R32G32B32_FLOAT,    0, offsetof(TriangleVertex, position) },
        { "COLOR",    0, ERHIFormat::R32G32B32A32_FLOAT, 0, offsetof(TriangleVertex, color) },
    };
    psoDesc.vertexBuffers = { { sizeof(TriangleVertex), false } };
    psoDesc.rasterizer.cullMode = ERHICullMode::None;
    // 深度バッファはまだ作らないので、深度テストも切る
    psoDesc.depthStencil.depthTestEnable = false;
    psoDesc.depthStencil.depthWriteEnable = false;
    psoDesc.dsvFormat = ERHIFormat::Unknown;
    psoDesc.numRenderTargets = 1;
    psoDesc.rtvFormat[0] = m_SwapChain->GetFormat();
    psoDesc.debugName = "TrianglePSO";

    m_TrianglePipeline = m_Device->CreatePipelineState(psoDesc);
    if (!m_TrianglePipeline) {
        throw std::runtime_error("PSO の生成に失敗しました");
    }
}

void Renderer::CreateTriangleVertexBuffer() {
    const TriangleVertex vertices[] = {
        { {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { {  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };

    // 書き換えない 3 頂点だけなので、GpuOnly への転送はせず Upload ヒープに置いたまま使う
    RHIBufferDesc desc{};
    desc.size = sizeof(vertices);
    desc.usage = ERHIBufferUsage::VertexBuffer;
    desc.memory = ERHIMemoryType::Upload;
    desc.debugName = "TriangleVB";
    m_TriangleVertexBuffer = m_Device->CreateBuffer(desc, vertices);
    if (!m_TriangleVertexBuffer) {
        throw std::runtime_error("頂点バッファの生成に失敗しました");
    }
}
