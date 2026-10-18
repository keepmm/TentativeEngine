/**
 * \file Window.hpp
 * \brief Win32 のメインウィンドウ
 */
#pragma once

#include <Windows.h>

#include <functional>
#include <string>
#include <utility>

// ウィンドウの生成とメッセージ処理だけを担当する。描画は持たない。
// リサイズは通知（コールバック）で上位に伝え、描画側の作り直しは上位が決める。
class Window {
public:
    using ResizeCallback = std::function<void(UINT width, UINT height)>;

    Window(HINSTANCE instance, const std::wstring& title, UINT width, UINT height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 溜まっているメッセージをすべて処理する。WM_QUIT を受けたら false を返す
    bool PumpMessages();

    HWND GetHandle() const { return m_Hwnd; }
    UINT GetWidth() const { return m_Width; }
    UINT GetHeight() const { return m_Height; }
    bool IsMinimized() const { return m_Minimized; }

    void SetResizeCallback(ResizeCallback callback) { m_OnResize = std::move(callback); }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_Hwnd = nullptr;
    UINT m_Width = 0;
    UINT m_Height = 0;
    bool m_Minimized = false;
    ResizeCallback m_OnResize;
};
