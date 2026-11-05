/**
 * \file Window.cpp
 * \brief Win32 ウィンドウの生成とメッセージ処理
 */
#include "Platform/Window.hpp"

#include <stdexcept>

namespace {
// ウィンドウクラス名。RegisterClassEx の登録名と CreateWindowEx で同じ文字列を使う
constexpr wchar_t kWindowClassName[] = L"TentativeEngineWindow";
}

Window::Window(HINSTANCE instance, const std::wstring& title, UINT width, UINT height)
    : m_Width(width), m_Height(height) {
    // ウィンドウクラスは「ウィンドウの種類」の登録。WndProc（メッセージの受け口）もここで渡す
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&wc)) {
        throw std::runtime_error("RegisterClassExW failed");
    }

    // WS_OVERLAPPEDWINDOW の枠はクライアント領域の外側にある。
    // 枠の分を足しておかないと、描画領域が指定した width x height より小さくなる
    RECT rect{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    // 最後の引数 this は WM_NCCREATE で WndProc に渡される。
    // ウィンドウ生成中は ハンドルがまだ返っていないので、this を経由して自分を取り出す
    m_Hwnd = CreateWindowExW(
        0, kWindowClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, this);
    if (!m_Hwnd) {
        throw std::runtime_error("CreateWindowExW failed");
    }

    ShowWindow(m_Hwnd, SW_SHOW);
}

Window::~Window() {
    // すでに閉じられているハンドルに対しては失敗するだけで害はない
    if (m_Hwnd) {
        DestroyWindow(m_Hwnd);
    }
}

bool Window::PumpMessages(float deltaSeconds) {
    // 先に前フレームの状態を保存してから、このフレームに届いたメッセージで今の状態にする
    m_Input.BeginFrame(deltaSeconds);

    // PeekMessage は待たずに戻るので、メッセージがなくてもフレームを回せる。
    // GetMessage だと次のメッセージが来るまでブロックしてしまう
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        // 生成時に渡した this を GWLP_USERDATA に保存する。
        // 以降のメッセージは、ここから Window* を取り出して C++ 側で処理する
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Window::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 先にフック（ImGui など）へ見せる。使われたメッセージは Input にも渡さない
    if (m_MessageHook && m_MessageHook(hwnd, msg, wParam, lParam)) {
        return TRUE;
    }

    // 入力はまず Input に記録だけさせる。WM_SYSKEYDOWN（Alt+F4 など）は DefWindowProc まで流さないと
    // ウィンドウが閉じられなくなるので、ここでは return しない
    m_Input.OnMessage(msg, wParam, lParam);

    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        // ドラッグ中にカーソルがウィンドウの外へ出ても、離すまでマウスのメッセージを受け取り続ける
        if (GetCapture() != hwnd) {
            SetCapture(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        // 全部のボタンを離したときだけ外す（左右を同時押しして片方だけ離したときに外れないように）
        if (!m_Input.AnyMouseButton() && GetCapture() == hwnd) {
            ReleaseCapture();
        }
        return 0;
    case WM_SIZE: {
        // 最小化時は幅・高さが 0 で届く。0 サイズでスワップチェインを作ると失敗するので、
        // フラグだけ立てて、描画の停止は Engine 側に任せる
        if (wParam == SIZE_MINIMIZED) {
            m_Minimized = true;
            return 0;
        }
        m_Minimized = false;
        m_Width = LOWORD(lParam);
        m_Height = HIWORD(lParam);
        if (m_OnResize && m_Width > 0 && m_Height > 0) {
            m_OnResize(m_Width, m_Height);
        }
        return 0;
    }
    case WM_DESTROY:
        // ウィンドウが破棄されたら WM_QUIT を投げて、PumpMessages を終わらせる
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
