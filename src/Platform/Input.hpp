/**
 * \file Input.hpp
 * \brief キーボード・マウス・ゲームパッドの入力状態（DirectX12Engine の Input を移植）
 */
#pragma once

#include "Math/Vector2.hpp"

#include <cstdint>

namespace TE {

    // XInput のボタンのビットをそのまま使う。トリガーは XInput ではボタンでないので、空いているビットを借りる
    namespace PadButton {
        enum : std::uint16_t {
            None      = 0x0000,
            DPadUp    = 0x0001,
            DPadDown  = 0x0002,
            DPadLeft  = 0x0004,
            DPadRight = 0x0008,
            Start     = 0x0010,
            Back      = 0x0020,
            LStick    = 0x0040,
            RStick    = 0x0080,
            LShoulder = 0x0100,
            RShoulder = 0x0200,
            LTrigger  = 0x0400,  // XInput の未使用ビット
            RTrigger  = 0x0800,  // 同上
            A         = 0x1000,
            B         = 0x2000,
            X         = 0x4000,
            Y         = 0x8000,
        };
    }

    // スティックは -1〜1、トリガーは 0〜1
    enum class PadAxis : std::uint8_t {
        LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger,
        Count,
    };

    namespace MouseButton {
        enum : int { Left = 0, Right = 1, Middle = 2, Count = 3 };
    }

    // Window がメッセージで書き込み、System は const 参照で読むだけ。
    // 1 フレームの流れ: BeginFrame（前フレームを保存・移動量を 0 に）→ OnMessage を何回か → System が読む
    // ヘッダに Windows.h を入れないよう、メッセージの引数は同じ大きさの標準の整数型で受け取る
    class Input {
    public:
        static constexpr int kPadCount = 4;  // XInput がつなげる最大数

        // ---- Window から呼ぶ ----
        void BeginFrame(float deltaSeconds);
        void OnMessage(std::uint32_t msg, std::uintptr_t wParam, std::intptr_t lParam);
        bool AnyMouseButton() const;

        // ---- キーボード（vk は 'A' や VK_SPACE などの仮想キーコード）----
        bool GetKey(int vk) const;      // 押している間
        bool GetKeyDown(int vk) const;  // 押したフレームだけ
        bool GetKeyUp(int vk) const;    // 離したフレームだけ

        // ---- マウス（button は MouseButton::Left など）----
        bool GetMouseButton(int button) const;
        bool GetMouseButtonDown(int button) const;
        bool GetMouseButtonUp(int button) const;
        Vector2 GetMousePosition() const { return m_MousePosition; }  // クライアント領域の左上が原点（ピクセル、下が +y）
        Vector2 GetMouseDelta() const { return m_MouseDelta; }        // このフレームで動いた量
        float GetMouseWheel() const { return m_MouseWheel; }          // 奥へ回すと正。1 ノッチ = 1

        // ---- ゲームパッド ----
        // 1 台ぶんの状態を読む窓口。中身は Input にあるので、値で受け渡してよい
        struct Pad {
            const Input* input = nullptr;
            int index = 0;

            bool Connected() const;
            bool IsPressed(std::uint16_t button) const;
            bool Down(std::uint16_t button) const;
            bool Up(std::uint16_t button) const;
            float Axis(PadAxis axis) const;
            Vector2 LeftStick() const { return { Axis(PadAxis::LeftX), Axis(PadAxis::LeftY) }; }
            Vector2 RightStick() const { return { Axis(PadAxis::RightX), Axis(PadAxis::RightY) }; }
            // left: 低周波モーター（重い揺れ）/ right: 高周波モーター（細かい揺れ）。0〜1。
            // seconds が 0 なら止めるまで振り続ける
            void SetVibration(float left, float right, float seconds = 0.0f) const;
        };
        Pad GetPad(int index = 0) const { return { this, index }; }

    private:
        void UpdateMousePosition(std::intptr_t lParam);
        void ReleaseMouseButtons();
        void ReleaseAll();
        void UpdatePads(float deltaSeconds);

        bool m_Keys[256]{};
        bool m_PrevKeys[256]{};
        bool m_MouseButtons[MouseButton::Count]{};
        bool m_PrevMouseButtons[MouseButton::Count]{};
        Vector2 m_MousePosition;
        Vector2 m_MouseDelta;
        float m_MouseWheel = 0.0f;
        bool m_HasMousePosition = false;

        bool m_PadConnected[kPadCount]{};
        std::uint16_t m_PadButtons[kPadCount]{};
        std::uint16_t m_PrevPadButtons[kPadCount]{};
        float m_PadAxis[kPadCount][static_cast<int>(PadAxis::Count)]{};
        // 振動は const な Pad から始めるので mutable にする（元は const_cast だった）。
        // 入力の「状態」ではなく出力のタイマーなので、const でも書き換えてよい
        mutable float m_VibrationTimer[kPadCount]{};
        float m_PadScanTimer = 0.0f;
    };

}  // namespace TE
