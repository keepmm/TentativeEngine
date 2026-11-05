/**
 * \file Engine.hpp
 * \brief ウィンドウと描画をつないでメインループを回す
 */
#pragma once

#include "Core/Timer.hpp"
#include "ECS/World.hpp"
#include "Platform/Window.hpp"
#include "Reflection.hpp"

#include <memory>

class Renderer;
class ImGuiLayer;
class ImGuiRenderer;
class ConsolePanel;
class Editor;

// 各モジュール（Platform / Renderer / ECS ...）を持ち、つなぐのはここだけ。
// Window と Renderer はお互いを知らず、リサイズの受け渡しも Engine が仲介する
class Engine {
public:
    explicit Engine(HINSTANCE instance);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ウィンドウが閉じられるまでフレームを回す
    void Run();

private:
    // 動作確認用のシーン（カメラ 1 台と、回る立方体を並べたもの）
    void CreateSampleScene();

    // 1 フレームぶんの System を決まった順番で回す
    void Update(float deltaSeconds);

    Window m_Window;
    // ログを溜め始めるのは Renderer より前（起動時のログも Console に出す）。消えるのも Renderer の後
    std::unique_ptr<ConsolePanel> m_Console;
    std::unique_ptr<Renderer> m_Renderer;
    // 宣言の逆順に破棄される。ImGuiRenderer（RHI のリソース）→ ImGuiLayer（コンテキスト）→ Renderer（デバイス）の順にしたいので、この並び
    std::unique_ptr<ImGuiLayer> m_ImGuiLayer;
    std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;
    // コンポーネントの名前とフィールドの一覧。Editor が参照するので、Editor より先に作り、後に消える位置に置く
    TE::TypeRegistry m_Types;
    std::unique_ptr<Editor> m_Editor;
    // World は GPU のリソースを直接持たない（MeshHandle は番号だけ）ので、Renderer との破棄の順番は気にしなくてよい
    TE::World m_World;
    Timer m_Timer;
};