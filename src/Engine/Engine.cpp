/**
 * \file Engine.cpp
 * \brief ウィンドウと描画をつないでメインループを回す
 */
#include "Engine/Engine.hpp"

#include "Game/Spin.hpp"
#include "Math/Math.hpp"
#include "Renderer/MeshRenderer.hpp"
#include "Renderer/RenderSystem.hpp"
#include "Renderer/Renderer.hpp"
#include "Scene/Camera.hpp"
#include "Scene/Transform.hpp"
#include "Scene/TransformSystem.hpp"

using namespace TE;

namespace {
    constexpr UINT kInitialWidth = 1280;
    constexpr UINT kInitialHeight = 720;
}

Engine::Engine(HINSTANCE instance)
    : m_Window(instance, L"TentativeEngine", kInitialWidth, kInitialHeight) {
    m_Renderer = std::make_unique<Renderer>(
        ERHIBackend::Vulkan, m_Window.GetHandle(), m_Window.GetWidth(), m_Window.GetHeight());

    // Window は 0 サイズを通知しない（最小化はフラグだけ立てる）ので、そのまま渡してよい
    m_Window.SetResizeCallback([this](UINT width, UINT height) {
        m_Renderer->Resize(width, height);
        });

    CreateSampleScene();
}

Engine::~Engine() {
    // メンバは宣言の逆順に壊れるので Renderer が Window より先に消える。
    // その後に Window がリサイズを通知して、破棄済みの Renderer を触らないよう外しておく
    m_Window.SetResizeCallback(nullptr);
}

void Engine::Run() {
    while (m_Window.PumpMessages()) {
        const double deltaSeconds = m_Timer.Tick();

        // 最小化中はバックバッファが 0 サイズになるので描かない
        if (m_Window.IsMinimized()) {
            Sleep(16);
            continue;
        }
        Update(static_cast<float>(deltaSeconds));
    }
}

void Engine::Update(float deltaSeconds) {
    // 順番に意味がある: 動かす → 行列を計算する → 描くものを集める → GPU へ送る → 予約した変更を反映する
    SpinSystem(m_World, deltaSeconds);
    TransformSystem(m_World);
    RenderSystem(m_World, *m_Renderer);
    m_Renderer->Render();
    m_World.FlushDeferred();
}

void Engine::CreateSampleScene() {
    // カメラ: 斜め上から原点を見下ろす
    const Vector3 cameraPosition{ 0.0f, 8.0f, -16.0f };
    m_World.CreateEntity(
        Transform{ cameraPosition, Quaternion::LookRotation(Vector3::zero - cameraPosition) },
        Camera{});

    // 立方体を 11 × 11 に並べる。場所によって色と回る速さを変える
    const MeshHandle cube = m_Renderer->CubeMesh();
    constexpr int kHalf = 5;
    for (int z = -kHalf; z <= kHalf; ++z) {
        for (int x = -kHalf; x <= kHalf; ++x) {
            const float hue = static_cast<float>(x + kHalf) / static_cast<float>(kHalf * 2 + 1);
            const float speed = 30.0f + 15.0f * static_cast<float>((x + z + kHalf * 2) % 5);
            m_World.CreateEntity(
                Transform{ { static_cast<float>(x) * 1.5f, 0.0f, static_cast<float>(z) * 1.5f },
                           Quaternion::Euler(0.0f, static_cast<float>(x * 15), 0.0f),
                           Vector3::one * 0.8f },
                LocalToWorld{},
                MeshRenderer{ cube, Color::HSVToRGB(hue, 0.6f, 0.95f) },
                Spin{ Vector3{ 0.3f, 1.0f, 0.0f }.Normalized(), speed });
        }
    }
}