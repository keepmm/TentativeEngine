/**
 * \file Input.cpp
 * \brief キーボード・マウス・ゲームパッドの入力状態
 */
#include "Platform/Input.hpp"

#include "Core/Log.hpp"

#include <Windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM（LOWORD だと負の座標が正の大きな数になる）
#include <Xinput.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>

namespace TE {

    namespace {
        // スティックのデッドゾーン（XInput の推奨値を 0〜1 に直したもの）
        constexpr float kStickDeadZone = 7849.0f / 32767.0f;
        constexpr float kTriggerDeadZone = 30.0f / 255.0f;
        constexpr float kTriggerButtonThreshold = 0.5f;  // これを超えたらトリガーをボタンとして扱う
        constexpr float kPadScanInterval = 1.0f;         // 未接続のパッドを探し直す間隔（秒）

        // デッドゾーンを抜いて -1〜1 に均す。
        // 単純に切り捨てると境界で 0 → 0.24 と値が飛ぶので、残りの範囲を引き伸ばしてつなぐ
        float ApplyDeadZone(float value, float deadZone) {
            const float magnitude = std::fabs(value);
            if (magnitude <= deadZone) {
                return 0.0f;
            }
            const float scaled = (magnitude - deadZone) / (1.0f - deadZone);
            return value < 0.0f ? -scaled : scaled;
        }

        float NormalizeStick(SHORT raw) {
            // -32768 側が 1 だけ範囲が広いので、-1 で止めて対称にする
            return std::max(-1.0f, static_cast<float>(raw) / 32767.0f);
        }

        bool IsValidKey(int vk) { return vk >= 0 && vk < 256; }
        bool IsValidButton(int button) { return button >= 0 && button < MouseButton::Count; }
    }  // namespace

    // ---------------------------------------------------//
    //                 フレーム / メッセージ                //
    // ---------------------------------------------------//

    void Input::BeginFrame(float deltaSeconds) {
        std::memcpy(m_PrevKeys, m_Keys, sizeof(m_Keys));
        std::memcpy(m_PrevMouseButtons, m_MouseButtons, sizeof(m_MouseButtons));
        // 移動量とホイールは「このフレームに届いたメッセージの合計」なので、毎フレーム 0 から数える
        m_MouseDelta = Vector2{};
        m_MouseWheel = 0.0f;
        UpdatePads(deltaSeconds);
    }

    void Input::OnMessage(std::uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam) {
        switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:  // Alt と一緒に押したキーと F10 はこちらに来る
            m_Keys[wParam & 0xFF] = true;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            m_Keys[wParam & 0xFF] = false;
            break;

        case WM_LBUTTONDOWN: m_MouseButtons[MouseButton::Left] = true;    UpdateMousePosition(lParam); break;
        case WM_RBUTTONDOWN: m_MouseButtons[MouseButton::Right] = true;   UpdateMousePosition(lParam); break;
        case WM_MBUTTONDOWN: m_MouseButtons[MouseButton::Middle] = true;  UpdateMousePosition(lParam); break;
        case WM_LBUTTONUP:   m_MouseButtons[MouseButton::Left] = false;   UpdateMousePosition(lParam); break;
        case WM_RBUTTONUP:   m_MouseButtons[MouseButton::Right] = false;  UpdateMousePosition(lParam); break;
        case WM_MBUTTONUP:   m_MouseButtons[MouseButton::Middle] = false; UpdateMousePosition(lParam); break;

        case WM_MOUSEMOVE:
            UpdateMousePosition(lParam);
            break;
        case WM_MOUSEWHEEL:
            // 1 ノッチ = WHEEL_DELTA（120）。高精度ホイールは 120 より細かく届くので、割って足し合わせる
            m_MouseWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            break;

        case WM_CAPTURECHANGED:
            // Alt+Tab などでキャプチャを取られると、ボタンを離したメッセージが来ない。
            // 押したままの扱いが残らないよう、ここで離したことにする（キーボードは触らない）
            ReleaseMouseButtons();
            break;
        case WM_KILLFOCUS:
            // フォーカスが外れると KEYUP が来ないので、全部離したことにする。
            // しないと W を押したまま切り替えたときに押しっぱなしで止まらない
            ReleaseAll();
            break;
        }
    }

    void Input::UpdateMousePosition(std::intptr_t lParam) {
        const Vector2 position{ static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) };
        // 最初の 1 回は前の位置が無いので、移動量に数えない（(0, 0) からの大きな跳びを防ぐ）
        if (m_HasMousePosition) {
            m_MouseDelta += position - m_MousePosition;
        }
        m_MousePosition = position;
        m_HasMousePosition = true;
    }

    void Input::ReleaseMouseButtons() {
        std::memset(m_MouseButtons, 0, sizeof(m_MouseButtons));
    }

    void Input::ReleaseAll() {
        std::memset(m_Keys, 0, sizeof(m_Keys));
        ReleaseMouseButtons();
    }

    bool Input::AnyMouseButton() const {
        return std::any_of(std::begin(m_MouseButtons), std::end(m_MouseButtons), [](bool down) { return down; });
    }

    // ---------------------------------------------------//
    //                  キーボード / マウス                 //
    // ---------------------------------------------------//

    bool Input::GetKey(int vk) const     { return IsValidKey(vk) && m_Keys[vk]; }
    bool Input::GetKeyDown(int vk) const { return IsValidKey(vk) && m_Keys[vk] && !m_PrevKeys[vk]; }
    bool Input::GetKeyUp(int vk) const   { return IsValidKey(vk) && !m_Keys[vk] && m_PrevKeys[vk]; }

    bool Input::GetMouseButton(int button) const {
        return IsValidButton(button) && m_MouseButtons[button];
    }
    bool Input::GetMouseButtonDown(int button) const {
        return IsValidButton(button) && m_MouseButtons[button] && !m_PrevMouseButtons[button];
    }
    bool Input::GetMouseButtonUp(int button) const {
        return IsValidButton(button) && !m_MouseButtons[button] && m_PrevMouseButtons[button];
    }

    // ---------------------------------------------------//
    //                     ゲームパッド                    //
    // ---------------------------------------------------//

    // 未接続の番号への XInputGetState は数百μs かかることがあるので、
    // つながっていないパッドは kPadScanInterval おきにしか探し直さない
    void Input::UpdatePads(float deltaSeconds) {
        m_PadScanTimer -= deltaSeconds;
        const bool rescan = m_PadScanTimer <= 0.0f;
        if (rescan) {
            m_PadScanTimer = kPadScanInterval;
        }

        for (int i = 0; i < kPadCount; ++i) {
            m_PrevPadButtons[i] = m_PadButtons[i];

            // 振動の時間切れ。元は接続中のときしか減らしていなかったので、抜いたまま再接続すると振動が残った
            if (m_VibrationTimer[i] > 0.0f) {
                m_VibrationTimer[i] -= deltaSeconds;
                if (m_VibrationTimer[i] <= 0.0f) {
                    m_VibrationTimer[i] = 0.0f;
                    XINPUT_VIBRATION stop{};
                    XInputSetState(static_cast<DWORD>(i), &stop);
                }
            }

            if (!m_PadConnected[i] && !rescan) {
                m_PadButtons[i] = 0;
                continue;
            }

            const bool wasConnected = m_PadConnected[i];
            XINPUT_STATE state{};
            if (XInputGetState(static_cast<DWORD>(i), &state) != ERROR_SUCCESS) {
                m_PadConnected[i] = false;
                m_PadButtons[i] = 0;
                std::fill(std::begin(m_PadAxis[i]), std::end(m_PadAxis[i]), 0.0f);
                if (wasConnected) {
                    LogWarning("[Input] ゲームパッド " + std::to_string(i) + " が外れました");
                    m_VibrationTimer[i] = 0.0f;  // 振動したまま抜かれたときのため
                }
                continue;
            }

            m_PadConnected[i] = true;
            if (!wasConnected) {
                LogInfo("[Input] ゲームパッド " + std::to_string(i) + " をつなぎました");
            }

            const XINPUT_GAMEPAD& pad = state.Gamepad;
            float* axis = m_PadAxis[i];
            axis[static_cast<int>(PadAxis::LeftX)]  = ApplyDeadZone(NormalizeStick(pad.sThumbLX), kStickDeadZone);
            axis[static_cast<int>(PadAxis::LeftY)]  = ApplyDeadZone(NormalizeStick(pad.sThumbLY), kStickDeadZone);
            axis[static_cast<int>(PadAxis::RightX)] = ApplyDeadZone(NormalizeStick(pad.sThumbRX), kStickDeadZone);
            axis[static_cast<int>(PadAxis::RightY)] = ApplyDeadZone(NormalizeStick(pad.sThumbRY), kStickDeadZone);
            const float leftTrigger  = ApplyDeadZone(pad.bLeftTrigger / 255.0f, kTriggerDeadZone);
            const float rightTrigger = ApplyDeadZone(pad.bRightTrigger / 255.0f, kTriggerDeadZone);
            axis[static_cast<int>(PadAxis::LeftTrigger)]  = leftTrigger;
            axis[static_cast<int>(PadAxis::RightTrigger)] = rightTrigger;

            // XInput のビットをそのまま使い、トリガーのぶんだけ自前のビットを足す
            std::uint16_t bits = pad.wButtons;
            if (leftTrigger > kTriggerButtonThreshold)  { bits |= PadButton::LTrigger; }
            if (rightTrigger > kTriggerButtonThreshold) { bits |= PadButton::RTrigger; }
            m_PadButtons[i] = bits;
        }
    }

    bool Input::Pad::Connected() const {
        return index >= 0 && index < kPadCount && input->m_PadConnected[index];
    }
    bool Input::Pad::IsPressed(std::uint16_t button) const {
        return Connected() && (input->m_PadButtons[index] & button) != 0;
    }
    bool Input::Pad::Down(std::uint16_t button) const {
        return Connected() && (input->m_PadButtons[index] & button) != 0 && (input->m_PrevPadButtons[index] & button) == 0;
    }
    bool Input::Pad::Up(std::uint16_t button) const {
        return Connected() && (input->m_PadButtons[index] & button) == 0 && (input->m_PrevPadButtons[index] & button) != 0;
    }
    float Input::Pad::Axis(PadAxis axis) const {
        if (!Connected() || axis == PadAxis::Count) {
            return 0.0f;
        }
        return input->m_PadAxis[index][static_cast<int>(axis)];
    }
    void Input::Pad::SetVibration(float left, float right, float seconds) const {
        if (!Connected()) {
            return;
        }
        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed  = static_cast<WORD>(std::clamp(left, 0.0f, 1.0f) * 65535.0f);
        vibration.wRightMotorSpeed = static_cast<WORD>(std::clamp(right, 0.0f, 1.0f) * 65535.0f);
        XInputSetState(static_cast<DWORD>(index), &vibration);
        input->m_VibrationTimer[index] = seconds > 0.0f ? seconds : 0.0f;
    }

}  // namespace TE
