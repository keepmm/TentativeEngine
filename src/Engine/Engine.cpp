/**
 * \file Engine.cpp
 * \brief ウィンドウと描画をつないでメインループを回す
 */
#include "Engine/Engine.hpp"

#include "Asset/ModelImporter.hpp"
#include "Core/Log.hpp"
#include "Editor/ConsolePanel.hpp"
#include "Editor/Editor.hpp"
#include "Game/Spin.hpp"
#include "Math/Math.hpp"
#include "Platform/Paths.hpp"
#include "Renderer/MeshRenderer.hpp"
#include "Renderer/ModelUpload.hpp"
#include "Renderer/RenderSystem.hpp"
#include "Renderer/Renderer.hpp"
#include "Scene/Camera.hpp"
#include "Scene/OrbitCamera.hpp"
#include "Scene/Transform.hpp"
#include "Scene/TransformSystem.hpp"
#include "Scene/Name.hpp"
#include "Engine/EngineTypes.hpp"
#include "UI/ImGuiLayer.hpp"
#include "UI/ImGuiRenderer.hpp"

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

using namespace TE;

namespace {
    constexpr UINT kInitialWidth = 1280;
    constexpr UINT kInitialHeight = 720;

    // 動作確認用のモデル。assets フォルダの中の場所（assets は exe の場所からさかのぼって探す）
    constexpr const char* kSampleModelFile = "models/spot/spot.fbx";
    constexpr float kSampleModelScale = 1.0f;

    // 頂点をすべて囲む箱（モデルの空間）。カメラを向ける範囲を決めるのに使う
    Bounds ComputeBounds(const ModelCpuData& model) {
        if (model.vertices.empty()) {
            return {};
        }
        Bounds bounds{ model.vertices[0].position, Vector3{} };
        for (const ModelVertex& vertex : model.vertices) {
            bounds.Encapsulate(vertex.position);
        }
        return bounds;
    }
}

Engine::Engine(HINSTANCE instance)
    : m_Window(instance, L"TentativeEngine", kInitialWidth, kInitialHeight) {
    // 以降のログ（アダプタ名やシェーダーのエラーなど）をエディターの Console にも溜める
    m_Console = std::make_unique<ConsolePanel>();

    // シェーダーは exe の場所から親へさかのぼって探す。
    // 開発中はソースツリーの shaders/（保存すればそのまま反映される）、配布時は exe の横の shaders/ が見つかる
    const std::filesystem::path shaderDirectory = FindDirectoryUpward("shaders", Renderer::kMeshShaderFile);
    if (shaderDirectory.empty()) {
        throw std::runtime_error("shaders フォルダが見つかりません（exe の横か、その親のどこかに置く）");
    }
    m_Renderer = std::make_unique<Renderer>(
        ERHIBackend::D3D12, m_Window.GetHandle(), m_Window.GetWidth(), m_Window.GetHeight(), shaderDirectory);

    // Window は 0 サイズを通知しない（最小化はフラグだけ立てる）ので、そのまま渡してよい
    m_Window.SetResizeCallback([this](UINT width, UINT height) {
        m_Renderer->Resize(width, height);
        });

    // UI。Window と Renderer はどちらも ImGui を知らないので、フックを通してここでつなぐ
    m_ImGuiLayer = std::make_unique<ImGuiLayer>(m_Window.GetHandle());
    m_ImGuiRenderer = std::make_unique<ImGuiRenderer>(m_Renderer->Device(), shaderDirectory);
    m_Window.SetMessageHook([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        return m_ImGuiLayer->HandleMessage(hwnd, msg, wParam, lParam);
        });
    m_Renderer->SetOverlayCallback([this](const Renderer::OverlayContext& context) {
        m_ImGuiRenderer->Record(context.commandList, ImGui::GetDrawData(),
            context.colorFormat, context.depthFormat, context.frameSlot);
        });
    // 型番号は初めて使ったときに決まるので、シーンを作る前に全部登録して確定させる
    RegisterEngineTypes(m_Types);
    m_Editor = std::make_unique<Editor>(*m_Renderer, *m_ImGuiRenderer, m_World, m_Types, *m_Console);

    CreateSampleScene();
}

Engine::~Engine() {
    // メンバは宣言の逆順に壊れるので Renderer が Window より先に消える。
    // その後に Window がリサイズを通知して、破棄済みの Renderer を触らないよう外しておく
    m_Window.SetResizeCallback(nullptr);
    // ImGui は Renderer より先に消えるので、消えた後に呼ばれないよう外しておく
    m_Window.SetMessageHook(nullptr);
    m_Renderer->SetOverlayCallback(nullptr);
}

void Engine::Run() {
    for (;;) {
        const double deltaSeconds = m_Timer.Tick();
        // 入力のフレームの区切りも PumpMessages の中で行う（パッドの再接続の間隔に時間を使う）
        if (!m_Window.PumpMessages(static_cast<float>(deltaSeconds))) {
            break;
        }

        // 最小化中はバックバッファが 0 サイズになるので描かない
        if (m_Window.IsMinimized()) {
            Sleep(16);
            continue;
        }
        Update(static_cast<float>(deltaSeconds));
    }
}

void Engine::Update(float deltaSeconds) {
    // 順番に意味がある: 動かす（カメラは入力で）→ 行列を計算する → 描くものを集める → GPU へ送る → 予約した変更を反映する
    // UI のフレームを始める。WantCapture* は前のフレームの UI の状態で決まる
    m_ImGuiLayer->BeginFrame();

    SpinSystem(m_World, deltaSeconds);
    // Scene View の上で操作しているときだけカメラを動かす（パネルの上では動かさない）
    if (m_Editor->IsSceneViewActive()) {
        OrbitCameraSystem(m_World, m_Window.GetInput());
    }
    TransformSystem(m_World);
    // G: グリッドの表示を切り替える（ModelViewer と同じキー）。UI の入力欄に文字を打っている間は反応させない
    if (!m_ImGuiLayer->WantCaptureKeyboard() && m_Window.GetInput().GetKeyDown('G')) {
        m_Renderer->SetGridVisible(!m_Renderer->IsGridVisible());
    }

    m_Editor->Draw(deltaSeconds);
    ImDrawData* uiDrawData = m_ImGuiLayer->EndFrame();
    // フォントなどを GPU に送る。送って待つので、Renderer がフレームの記録を始める前に済ませる
    m_ImGuiRenderer->UpdateTextures(uiDrawData);

    // 描く前に、保存されたシェーダーがあれば PSO を作り直しておく
    m_Renderer->PollShaderReload(deltaSeconds);
    RenderSystem(m_World, *m_Renderer);
    m_Renderer->Render();  // 最後にオーバーレイとして UI が描かれる
    m_World.FlushDeferred();
}

void Engine::CreateSampleScene() {
    // 立方体を 11 × 11 に並べる。場所によって色と回る速さを変える
    //const MeshHandle cube = m_Renderer->CubeMesh();
    //constexpr int kHalf = 5;
    //for (int z = -kHalf; z <= kHalf; ++z) {
    //    for (int x = -kHalf; x <= kHalf; ++x) {
    //        const float hue = static_cast<float>(x + kHalf) / static_cast<float>(kHalf * 2 + 1);
    //        const float speed = 30.0f + 15.0f * static_cast<float>((x + z + kHalf * 2) % 5);
    //        m_World.CreateEntity(
    //            Transform{ { static_cast<float>(x) * 1.5f, 0.0f, static_cast<float>(z) * 1.5f },
    //                       Quaternion::Euler(0.0f, static_cast<float>(x * 15), 0.0f),
    //                       Vector3::one * 0.8f },
    //            LocalToWorld{},
    //            MeshRenderer{ cube, Color::HSVToRGB(hue, 0.6f, 0.95f) },
    //            Spin{ Vector3{ 0.3f, 1.0f, 0.0f }.Normalized(), speed });
    //    }
    //}

    // カメラを向ける範囲。モデルが読めなかったときは立方体の並び全体（11 個 × 1.5 間隔 ≒ 16.5）
    Bounds focus = Bounds::FromCenterSize(Vector3::zero, Vector3{ 16.5f, 2.0f, 16.5f });

    // 読み込んだモデルを立方体の上に 1 つ置く。見つからない・読めないときも立方体は描けるので、ログだけ出して続ける。
    // assets もシェーダーと同じく、exe の場所からさかのぼって探す
    const std::filesystem::path assetDirectory = FindDirectoryUpward("assets", kSampleModelFile);
    std::optional<ModelCpuData> model;
    if (!assetDirectory.empty()) {
        model = ImportModel(assetDirectory / kSampleModelFile, kSampleModelScale);
    }
    if (model) {
        const Vector3 modelPosition{ 0.0f, 3.0f, 0.0f };
        const MeshHandle modelMesh = CreateMeshFromModel(*m_Renderer, *model);
        m_World.CreateEntity(
            Name{ "Spot" },
            Transform{ modelPosition, Quaternion::Euler(0.0f, 0.0f, 0.0f), Vector3::one },
            LocalToWorld{},
            MeshRenderer{ modelMesh, Color::white },
            Spin{ Vector3::up, 30.0f });

        // 置いた位置へずらすだけでよい（拡大率は 1）。Spin で回ると箱も少しずれるが、カメラを向ける目安なので気にしない
        focus = ComputeBounds(*model);
        focus.center += modelPosition;
    } else {
        LogError(std::string("[Engine] サンプルモデルを読み込めませんでした: assets/") + kSampleModelFile);
    }

    // カメラ: 正面（-Z 側）の少し上から見下ろし、focus 全体が画面に入る距離に置く。
    // Transform は OrbitCameraSystem が毎フレーム上書きするので、最初の値は何でもよい
    OrbitCamera orbit;
    orbit.view = FrameBounds(focus, 0.0f, 25.0f, orbit);
    orbit.home = orbit.view;
    m_World.CreateEntity(Name{ "Main Camera" }, Transform{}, Camera{}, orbit);
}