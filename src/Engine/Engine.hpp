/**
 * \file Engine.hpp
 * \brief ウィンドウと描画をつないでメインループを回す
 */
#pragma once

#include "Core/Timer.hpp"
#include "Platform/Window.hpp"

#include <memory>

class Renderer;

// 各モジュール（Platform / Renderer ...）を持ち、つなぐのはここだけ。
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
    Window m_Window;
    std::unique_ptr<Renderer> m_Renderer;
    Timer m_Timer;
};
