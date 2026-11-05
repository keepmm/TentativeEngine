#include "Core/Platform.hpp"

#if TE_PLATFORM_ANDROID

#include "Platform/Window.hpp"

#include <android/input.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <cmath>
#include <stdexcept>

Window::Window(NativeAppHandle app, const std::wstring&, std::uint32_t, std::uint32_t)
    : m_App(app) {
    app->userData = this;
    app->onAppCmd = OnAppCommand;
    app->onInputEvent = OnInputEvent;

    // Surface はウィンドウが届くまで作れないので、ここで待つ
    while (app->window == nullptr) {
        if (app->destroyRequested) {
            throw std::runtime_error("ウィンドウが届く前に終了しました");
        }
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source) {
            source->process(app, source);
        }
    }
    m_Handle = app->window;
    m_Width = static_cast<std::uint32_t>(ANativeWindow_getWidth(app->window));
    m_Height = static_cast<std::uint32_t>(ANativeWindow_getHeight(app->window));
}

Window::~Window() {
    m_App->onAppCmd = nullptr;
    m_App->onInputEvent = nullptr;
    m_App->userData = nullptr;
}

bool Window::PumpMessages(float deltaSeconds) {
    m_Input.BeginFrame(deltaSeconds);
    android_poll_source* source = nullptr;
    while (ALooper_pollOnce(0, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0) {
        if (source) {
            source->process(m_App, source);
        }
        if (m_App->destroyRequested) {
            return false;
        }
    }
    return !m_App->destroyRequested;
}

void Window::OnAppCommand(android_app* app, std::int32_t command) {
    auto* self = static_cast<Window*>(app->userData);
    if (!self) {
        return;
    }
    switch (command) {
    case APP_CMD_TERM_WINDOW:
        // 今は Surface を作り直せないので、バックグラウンドに回ったらアプリを終える
        self->m_Minimized = true;
        self->m_Closing = true;
        ANativeActivity_finish(app->activity);
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        if (app->window && !self->m_Closing) {
            const auto width = static_cast<std::uint32_t>(ANativeWindow_getWidth(app->window));
            const auto height = static_cast<std::uint32_t>(ANativeWindow_getHeight(app->window));
            if ((width != self->m_Width || height != self->m_Height) && width > 0 && height > 0) {
                self->m_Width = width;
                self->m_Height = height;
                if (self->m_OnResize) {
                    self->m_OnResize(width, height);
                }
            }
        }
        break;
    case APP_CMD_LOST_FOCUS:
        self->m_Input.OnTouchEnd();
        break;
    default:
        break;
    }
}

std::int32_t Window::OnInputEvent(android_app* app, AInputEvent* event) {
    auto* self = static_cast<Window*>(app->userData);
    if (!self || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
        return 0;
    }
    const std::int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    const std::size_t count = AMotionEvent_getPointerCount(event);

    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        self->m_Input.OnTouchEnd();
        self->m_PinchDistance = 0.0f;
        return 1;
    }
    if (count >= 2) {
        // 2 本指はピンチでズーム。回転は止める（指を置いた瞬間に跳ばないように）
        self->m_Input.OnTouchEnd();
        const float dx = AMotionEvent_getX(event, 1) - AMotionEvent_getX(event, 0);
        const float dy = AMotionEvent_getY(event, 1) - AMotionEvent_getY(event, 0);
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (self->m_PinchDistance > 0.0f) {
            constexpr float kPixelsPerNotch = 100.0f;
            self->m_Input.OnPinch((distance - self->m_PinchDistance) / kPixelsPerNotch);
        }
        self->m_PinchDistance = distance;
        return 1;
    }
    self->m_PinchDistance = 0.0f;
    self->m_Input.OnTouch(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
    return 1;
}

#endif