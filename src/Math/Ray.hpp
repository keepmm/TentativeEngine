/**
 * \file Ray.hpp
 * \brief 半直線（Unity の Ray 相当）
 */
#pragma once

#include "Math/Vector3.hpp"

namespace TE {

    // origin から direction の方向へ無限にのびる線。マウスでの選択やレイキャストに使う
    struct Ray {
        Vector3 origin;
        Vector3 direction{ 0.0f, 0.0f, 1.0f };  // 常に長さ 1

        Ray() = default;

        // 交差判定の距離が「ワールドの長さ」になるよう、向きは正規化して持つ
        Ray(const Vector3& rayOrigin, const Vector3& rayDirection)
            : origin(rayOrigin), direction(rayDirection.Normalized()) {}

        // origin から distance だけ進んだ点
        constexpr Vector3 GetPoint(float distance) const { return origin + direction * distance; }
    };

}  // namespace TE