/**
 * \file VectorInt.hpp
 * \brief 整数のベクトル（Unity の Vector2Int / Vector3Int 相当）
 */
#pragma once

#include "Math/Vector2.hpp"
#include "Math/Vector3.hpp"

namespace TE {

    // ピクセル座標・テクスチャサイズ・グリッドのマス目など、誤差があっては困るもの用
    struct Vector2Int {
        int x = 0;
        int y = 0;

        static const Vector2Int zero;
        static const Vector2Int one;
        static const Vector2Int up;
        static const Vector2Int down;
        static const Vector2Int left;
        static const Vector2Int right;

        friend constexpr Vector2Int operator+(const Vector2Int& a, const Vector2Int& b) { return { a.x + b.x, a.y + b.y }; }
        friend constexpr Vector2Int operator-(const Vector2Int& a, const Vector2Int& b) { return { a.x - b.x, a.y - b.y }; }
        friend constexpr Vector2Int operator-(const Vector2Int& v) { return { -v.x, -v.y }; }
        friend constexpr Vector2Int operator*(const Vector2Int& v, int s) { return { v.x * s, v.y * s }; }
        friend constexpr Vector2Int operator*(int s, const Vector2Int& v) { return v * s; }
        friend constexpr Vector2Int& operator+=(Vector2Int& a, const Vector2Int& b) { return a = a + b; }
        friend constexpr Vector2Int& operator-=(Vector2Int& a, const Vector2Int& b) { return a = a - b; }
        friend constexpr bool operator==(const Vector2Int& a, const Vector2Int& b) { return a.x == b.x && a.y == b.y; }
        friend constexpr bool operator!=(const Vector2Int& a, const Vector2Int& b) { return !(a == b); }

        constexpr int SqrMagnitude() const { return x * x + y * y; }
        float Magnitude() const { return Mathf::Sqrt(static_cast<float>(SqrMagnitude())); }
        constexpr Vector2 ToVector2() const { return { static_cast<float>(x), static_cast<float>(y) }; }

        static float Distance(const Vector2Int& a, const Vector2Int& b) { return (a - b).Magnitude(); }
        static constexpr Vector2Int Scale(const Vector2Int& a, const Vector2Int& b) { return { a.x * b.x, a.y * b.y }; }
        static constexpr Vector2Int Min(const Vector2Int& a, const Vector2Int& b) { return { Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y) }; }
        static constexpr Vector2Int Max(const Vector2Int& a, const Vector2Int& b) { return { Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y) }; }

        static Vector2Int FloorToInt(const Vector2& v) { return { Mathf::FloorToInt(v.x), Mathf::FloorToInt(v.y) }; }
        static Vector2Int CeilToInt(const Vector2& v) { return { Mathf::CeilToInt(v.x), Mathf::CeilToInt(v.y) }; }
        static Vector2Int RoundToInt(const Vector2& v) { return { Mathf::RoundToInt(v.x), Mathf::RoundToInt(v.y) }; }
    };

    inline constexpr Vector2Int Vector2Int::zero{ 0, 0 };
    inline constexpr Vector2Int Vector2Int::one{ 1, 1 };
    inline constexpr Vector2Int Vector2Int::up{ 0, 1 };
    inline constexpr Vector2Int Vector2Int::down{ 0, -1 };
    inline constexpr Vector2Int Vector2Int::left{ -1, 0 };
    inline constexpr Vector2Int Vector2Int::right{ 1, 0 };

    struct Vector3Int {
        int x = 0;
        int y = 0;
        int z = 0;

        static const Vector3Int zero;
        static const Vector3Int one;
        static const Vector3Int up;
        static const Vector3Int down;
        static const Vector3Int left;
        static const Vector3Int right;
        static const Vector3Int forward;
        static const Vector3Int back;

        friend constexpr Vector3Int operator+(const Vector3Int& a, const Vector3Int& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        friend constexpr Vector3Int operator-(const Vector3Int& a, const Vector3Int& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        friend constexpr Vector3Int operator-(const Vector3Int& v) { return { -v.x, -v.y, -v.z }; }
        friend constexpr Vector3Int operator*(const Vector3Int& v, int s) { return { v.x * s, v.y * s, v.z * s }; }
        friend constexpr Vector3Int operator*(int s, const Vector3Int& v) { return v * s; }
        friend constexpr Vector3Int& operator+=(Vector3Int& a, const Vector3Int& b) { return a = a + b; }
        friend constexpr Vector3Int& operator-=(Vector3Int& a, const Vector3Int& b) { return a = a - b; }
        friend constexpr bool operator==(const Vector3Int& a, const Vector3Int& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
        friend constexpr bool operator!=(const Vector3Int& a, const Vector3Int& b) { return !(a == b); }

        constexpr int SqrMagnitude() const { return x * x + y * y + z * z; }
        float Magnitude() const { return Mathf::Sqrt(static_cast<float>(SqrMagnitude())); }
        constexpr Vector3 ToVector3() const { return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) }; }

        static float Distance(const Vector3Int& a, const Vector3Int& b) { return (a - b).Magnitude(); }
        static constexpr Vector3Int Scale(const Vector3Int& a, const Vector3Int& b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
        static constexpr Vector3Int Min(const Vector3Int& a, const Vector3Int& b) {
            return { Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y), Mathf::Min(a.z, b.z) };
        }
        static constexpr Vector3Int Max(const Vector3Int& a, const Vector3Int& b) {
            return { Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y), Mathf::Max(a.z, b.z) };
        }

        static Vector3Int FloorToInt(const Vector3& v) { return { Mathf::FloorToInt(v.x), Mathf::FloorToInt(v.y), Mathf::FloorToInt(v.z) }; }
        static Vector3Int CeilToInt(const Vector3& v) { return { Mathf::CeilToInt(v.x), Mathf::CeilToInt(v.y), Mathf::CeilToInt(v.z) }; }
        static Vector3Int RoundToInt(const Vector3& v) { return { Mathf::RoundToInt(v.x), Mathf::RoundToInt(v.y), Mathf::RoundToInt(v.z) }; }
    };

    inline constexpr Vector3Int Vector3Int::zero{ 0, 0, 0 };
    inline constexpr Vector3Int Vector3Int::one{ 1, 1, 1 };
    inline constexpr Vector3Int Vector3Int::up{ 0, 1, 0 };
    inline constexpr Vector3Int Vector3Int::down{ 0, -1, 0 };
    inline constexpr Vector3Int Vector3Int::left{ -1, 0, 0 };
    inline constexpr Vector3Int Vector3Int::right{ 1, 0, 0 };
    inline constexpr Vector3Int Vector3Int::forward{ 0, 0, 1 };
    inline constexpr Vector3Int Vector3Int::back{ 0, 0, -1 };

}  // namespace TE