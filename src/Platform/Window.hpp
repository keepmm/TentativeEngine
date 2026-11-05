/**
 * \file Window.hpp
 * \brief Win32 のメインウィンドウ
 */
#pragma once

#include "Platform/Input.hpp"
#include "Core/Platform.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#if TE_PLATFORM_WINDOWS
#include <Windows.h>
using NativeAppHandle = HINSTANCE;
#else
struct android_app;
using NativeAppHandle = android_app*;
#endif

// ウィンドウの生成とメッセージ処理だけを担当する。描画は持たない。
// リサイズは通知（コールバック）で上位に伝え、描画側の作り直しは上位が決める。
class Window {
public:
    using ResizeCallback = std::function<void(std::uint32_t width, std::uint32_t height)>;

    Window(NativeAppHandle app,const std::wstring& title, 
        std::uint32_t width, std::uint32_t height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 溜まっているメッセージをすべて処理する。WM_QUIT を受けたら false を返す。
    // 最初に入力のフレームを区切るので、1 フレームに 1 回だけ呼ぶこと
    bool PumpMessages(float deltaSeconds);

#if TE_PLATFORM_WINDOWS
    HWND GetHandle() const { return m_Hwnd; }
#else
    void* GetHandle() const { return m_Handle; }
#endif
    std::uint32_t GetWidth() const { return m_Width; }
    std::uint32_t GetHeight() const { return m_Height; }
    bool IsMinimized() const { return m_Minimized; }
    // System はこれを読むだけ。書き込むのは Window のメッセージ処理だけにしておく
    const TE::Input& GetInput() const { return m_Input; }

    void SetResizeCallback(ResizeCallback callback) { m_OnResize = std::move(callback); }
#if TE_PLATFORM_WINDOWS
    // メッセージを Window より先に見る。true を返したら、そのメッセージは使われたものとして Window は何もしない
    using MessageHook = std::function<bool(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)>;
    void SetMessageHook(MessageHook hook) { m_MessageHook = std::move(hook); }
private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    MessageHook m_MessageHook;
    HWND m_Hwnd = nullptr;
#else
private:
    static void OnAppCommand(android_app* app, std::int32_t command);
    static std::int32_t OnInputEvent(android_app* app, struct AInputEvent* event);
    android_app* m_App = nullptr;
    void* m_Handle = nullptr;
    float m_PinchDistance = 0.0f;
    bool m_Closing = false;
#endif

    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;
    bool m_Minimized = false;
    ResizeCallback m_OnResize;
    TE::Input m_Input;
};
