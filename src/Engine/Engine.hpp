/**
 * \file Engine.hpp
 * \brief ウィンドウと描画をつないでメインループを回す
 */
#pragma once

#include "Core/Timer.hpp"
#include "ECS/World.hpp"
#include "Platform/Window.hpp"

#include <memory>

class Renderer;

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
    std::unique_ptr<Renderer> m_Renderer;
    // World は GPU のリソースを直接持たない（MeshHandle は番号だけ）ので、Renderer との破棄の順番は気にしなくてよい
    TE::World m_World;
    Timer m_Timer;
};