/**
 * \file Camera.hpp
 * \brief カメラのコンポーネント
 */
#pragma once

#include "Math/Color.hpp"

namespace TE {

    // 位置と向きは同じ Entity の Transform を使う（Unity の Camera と同じ）
    struct Camera {
        float fieldOfView = 60.0f;  // 縦の画角（度）
        float nearClip = 0.1f;
        float farClip = 1000.0f;
        // Renderer の既定値と同じ、暗い紺色
        Color clearColor{ 0.10f, 0.12f, 0.18f, 1.0f };
    };

}  // namespace TE