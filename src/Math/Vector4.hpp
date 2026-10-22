/**
 * \file Vector4.hpp
 * \brief 4 次元ベクトル（Unity の Vector4 相当）
 */
#pragma once

#include "Math/Vector3.hpp"

namespace TE {

    // 同次座標（w = 1 なら位置、w = 0 なら向き）や、シェーダーへ渡す float4 に使う
    struct Vector4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        static const Vector4 zero;
        static const Vector4 one;

        // ---- 演算子 ---- //
        friend constexpr Vector4 operator+(const Vector4& a, const Vector4& b) { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
        friend constexpr Vector4 operator-(const Vector4& a, const Vector4& b) { return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
        friend constexpr Vector4 operator-(const Vector4& v) { return { -v.x, -v.y, -v.z, -v.w }; }
        friend constexpr Vector4 operator*(const Vector4& v, float s) { return { v.x * s, v.y * s, v.z * s, v.w * s }; }
        friend constexpr Vector4 operator*(float s, const Vector4& v) { return v * s; }
        friend constexpr Vector4 operator/(const Vector4& v, float s) { return { v.x / s, v.y / s, v.z / s, v.w / s }; }
        friend constexpr Vector4& operator+=(Vector4& a, const Vector4& b) { return a = a + b; }
        friend constexpr Vector4& operator-=(Vector4& a, const Vector4& b) { return a = a - b; }
        friend constexpr Vector4& operator*=(Vector4& v, float s) { return v = v * s; }
        friend constexpr Vector4& operator/=(Vector4& v, float s) { return v = v / s; }

        constexpr float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
        constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }

        // ---- Vector3 との行き来 ---- //
        static constexpr Vector4 Point(const Vector3& v) { return { v.x, v.y, v.z, 1.0f }; }      // 位置（平行移動が効く）
        static constexpr Vector4 Direction(const Vector3& v) { return { v.x, v.y, v.z, 0.0f }; }  // 向き（平行移動が効かない）
        constexpr Vector3 XYZ() const { return { x, y, z }; }

        // ---- 長さ ---- //
        constexpr float SqrMagnitude() const { return Dot(*this, *this); }
        float Magnitude() const { return Mathf::Sqrt(SqrMagnitude()); }
        Vector4 Normalized() const {
            const float magnitude = Magnitude();
            return magnitude > 1e-5f ? *this / magnitude : Vector4{};
        }
        void Normalize() { *this = Normalized(); }

        // ---- static 関数 ---- //
        static constexpr float Dot(const Vector4& a, const Vector4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
        static constexpr Vector4 Scale(const Vector4& a, const Vector4& b) { return { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w }; }
        static constexpr Vector4 LerpUnclamped(const Vector4& a, const Vector4& b, float t) { return a + (b - a) * t; }
        static constexpr Vector4 Lerp(const Vector4& a, const Vector4& b, float t) {
            return LerpUnclamped(a, b, Mathf::Clamp01(t));
        }
    };

    inline constexpr Vector4 Vector4::zero{ 0.0f, 0.0f, 0.0f, 0.0f };
    inline constexpr Vector4 Vector4::one{ 1.0f, 1.0f, 1.0f, 1.0f };

}  // namespace TE