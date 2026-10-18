/**
 * \file Engine.cpp
 * \brief ウィンドウと描画をつないでメインループを回す
 */
#include "Engine/Engine.hpp"

#include "Renderer/Renderer.hpp"

namespace {
constexpr UINT kInitialWidth = 1280;
constexpr UINT kInitialHeight = 720;
}

Engine::Engine(HINSTANCE instance)
    : m_Window(instance, L"TentativeEngine", kInitialWidth, kInitialHeight) {
    m_Renderer = std::make_unique<Renderer>(
        ERHIBackend::D3D12, m_Window.GetHandle(), m_Window.GetWidth(), m_Window.GetHeight());

    // Window は 0 サイズを通知しない（最小化はフラグだけ立てる）ので、そのまま渡してよい
    m_Window.SetResizeCallback([this](UINT width, UINT height) {
        m_Renderer->Resize(width, height);
    });
}

Engine::~Engine() {
    // メンバは宣言の逆順に壊れるので Renderer が Window より先に消える。
    // その後に Window がリサイズを通知して、破棄済みの Renderer を触らないよう外しておく
    m_Window.SetResizeCallback(nullptr);
}

void Engine::Run() {
    while (m_Window.PumpMessages()) {
        [[maybe_unused]] const double deltaSeconds = m_Timer.Tick();

        // 最小化中はバックバッファが 0 サイズになるので描かない
        if (m_Window.IsMinimized()) {
            Sleep(16);
            continue;
        }
        m_Renderer->Render();
    }
}
