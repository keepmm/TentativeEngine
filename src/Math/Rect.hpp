/**
 * \file Rect.hpp
 * \brief 2 次元の矩形（Unity の Rect 相当）
 */
#pragma once

#include "Math/Vector2.hpp"

namespace TE {

    // 左下（または左上。使う側の座標系に従う）の角 (x, y) と大きさで表す。UI・ビューポート・スプライトの切り出し用
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        // 角の座標から作る
        static constexpr Rect MinMaxRect(float xMin, float yMin, float xMax, float yMax) {
            return { xMin, yMin, xMax - xMin, yMax - yMin };
        }

        constexpr Vector2 Position() const { return { x, y }; }
        constexpr Vector2 Size() const { return { width, height }; }
        constexpr Vector2 Min() const { return { x, y }; }
        constexpr Vector2 Max() const { return { x + width, y + height }; }
        constexpr Vector2 Center() const { return { x + width * 0.5f, y + height * 0.5f }; }

        // 右端と上端はふくまない。隣り合う Rect の境目の点が、両方に入ってしまわないようにするため
        constexpr bool Contains(const Vector2& point) const {
            return point.x >= x && point.x < x + width && point.y >= y && point.y < y + height;
        }

        // 少しでも重なっていれば true（辺が接しているだけなら false）
        constexpr bool Overlaps(const Rect& other) const {
            return other.x < x + width && other.x + other.width > x
                && other.y < y + height && other.y + other.height > y;
        }

        // 0〜1 の正規化座標を、この Rect の中の座標に変える
        constexpr Vector2 NormalizedToPoint(const Vector2& normalized) const {
            return { x + width * normalized.x, y + height * normalized.y };
        }
    };

}  // namespace TE