/**
 * \file BoundingSphere.hpp
 * \brief 球（Unity の BoundingSphere 相当）
 */
#pragma once

#include "Math/Bounds.hpp"

namespace TE {

    // 回転しても形が変わらないので、回る物体の大まかな判定に向く
    struct BoundingSphere {
        Vector3 position;
        float radius = 0.0f;

        constexpr bool Contains(const Vector3& point) const { return (point - position).SqrMagnitude() <= radius * radius; }

        // 中心どうしの距離が半径の和以下なら重なっている（√ を避けて 2 乗で比べる）
        constexpr bool Intersects(const BoundingSphere& other) const {
            const float radiusSum = radius + other.radius;
            return (other.position - position).SqrMagnitude() <= radiusSum * radiusSum;
        }

        // 箱の中で球の中心に一番近い点が、球の中にあれば重なっている
        constexpr bool Intersects(const Bounds& bounds) const { return bounds.SqrDistance(position) <= radius * radius; }

        /// @brief レイと球の交差
        /// @param distance 球に入るまでの距離。レイの始点が球の中なら 0
        /// @return 前方で交わったら true
        bool IntersectRay(const Ray& ray, float& distance) const {
            // |o + d t - c|² = r² を t について解く（d は長さ 1 なので t² の係数は 1）
            const Vector3 toOrigin = ray.origin - position;
            const float b = Vector3::Dot(toOrigin, ray.direction);
            const float c = toOrigin.SqrMagnitude() - radius * radius;
            if (c > 0.0f && b > 0.0f) {
                return false;  // 始点が球の外で、球から遠ざかる向き
            }
            const float discriminant = b * b - c;
            if (discriminant < 0.0f) {
                return false;  // 判別式が負: 交わらない
            }
            distance = Mathf::Max(-b - Mathf::Sqrt(discriminant), 0.0f);
            return true;
        }
    };

}  // namespace TE