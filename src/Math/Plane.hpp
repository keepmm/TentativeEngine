/**
 * \file Plane.hpp
 * \brief 無限に広い平面（Unity の Plane 相当）
 */
#pragma once

#include "Math/Ray.hpp"

namespace TE {

    // Dot(normal, p) + distance = 0 を満たす点 p の集まり。
    // normal が向いている側が「表」で、点までの符号付き距離が正になる
    struct Plane {
        Vector3 normal{ 0.0f, 1.0f, 0.0f };  // 常に長さ 1
        float distance = 0.0f;

        // 法線と、面の上の 1 点から作る
        static Plane FromNormalAndPoint(const Vector3& inNormal, const Vector3& inPoint) {
            const Vector3 n = inNormal.Normalized();
            return { n, -Vector3::Dot(n, inPoint) };
        }

        // 3 点から作る。a → b → c が時計回りに見える側が表になる（D3D の表面と同じ向き）
        static Plane FromPoints(const Vector3& a, const Vector3& b, const Vector3& c) {
            return FromNormalAndPoint(Vector3::Cross(b - a, c - a), a);
        }

        // 符号付き距離。表側なら正、裏側なら負
        constexpr float GetDistanceToPoint(const Vector3& point) const { return Vector3::Dot(normal, point) + distance; }

        // 表側にあれば true
        constexpr bool GetSide(const Vector3& point) const { return GetDistanceToPoint(point) > 0.0f; }

        constexpr bool SameSide(const Vector3& a, const Vector3& b) const {
            const float da = GetDistanceToPoint(a);
            const float db = GetDistanceToPoint(b);
            return (da > 0.0f && db > 0.0f) || (da <= 0.0f && db <= 0.0f);
        }

        // 面の上で point に一番近い点（距離ぶん法線の逆へ戻す）
        constexpr Vector3 ClosestPointOnPlane(const Vector3& point) const {
            return point - normal * GetDistanceToPoint(point);
        }

        // 表と裏を入れ替える
        constexpr Plane Flipped() const { return { -normal, -distance }; }

        /// @brief レイと面の交差
        /// @param enter 交わった点までの距離。ray.GetPoint(enter) で交点が求まる
        /// @return 前方で交わったら true。面と平行、または後ろで交わるなら false
        bool Raycast(const Ray& ray, float& enter) const {
            // Dot(n, o + d t) + distance = 0 を t について解く
            const float denominator = Vector3::Dot(ray.direction, normal);
            if (Mathf::Abs(denominator) < 1e-6f) {
                enter = 0.0f;
                return false;
            }
            enter = -GetDistanceToPoint(ray.origin) / denominator;
            return enter > 0.0f;
        }
    };

}  // namespace TE