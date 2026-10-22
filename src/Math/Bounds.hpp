/*****************************************************************//**
 * \file   Bounds.hpp
 * \brief  軸に沿った箱 AABB
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "Matrix4x4.hpp"
#include "Ray.hpp"

namespace TE
{
	struct Bounds
	{
		Vector3 center;
		Vector3 extents; // 大きさの半分

		static constexpr Bounds FromCenterSize(const Vector3& center, const Vector3& size)
		{
			return { center,size * 0.5f };
		}

		static constexpr Bounds FromMinMax(const Vector3& min, const Vector3& max)
		{
			return { (min + max) * 0.5f,(max - min) * 0.5f };
		}

		constexpr Vector3 Size() const { return extents * 2.0f; }
		constexpr Vector3 Min() const { return center - extents; }
		constexpr Vector3 Max() const { return center + extents; }

		constexpr void SetMinMax(const Vector3& min, const Vector3& max) { *this = FromMinMax(min, max); }

		constexpr void Encapsulate(const Vector3& point)
		{
			SetMinMax(Vector3::Min(Min(), point), Vector3::Max(Max(), point));
		}
		constexpr void Encapsulate(const Bounds& other) {
			SetMinMax(Vector3::Min(Min(), other.Min()), Vector3::Max(Max(), other.Max()));
		}
		constexpr void Expand(float amount)
		{
			extents += Vector3::one * (amount * 0.5f);
		}

        // 境界の上もふくむ
        constexpr bool Contains(const Vector3& point) const {
            const Vector3 min = Min();
            const Vector3 max = Max();
            return point.x >= min.x && point.x <= max.x
                && point.y >= min.y && point.y <= max.y
                && point.z >= min.z && point.z <= max.z;
        }

        // 3 軸すべてで範囲が重なっていれば、箱どうしは重なっている
        constexpr bool Intersects(const Bounds& other) const {
            const Vector3 aMin = Min(), aMax = Max();
            const Vector3 bMin = other.Min(), bMax = other.Max();
            return aMin.x <= bMax.x && aMax.x >= bMin.x
                && aMin.y <= bMax.y && aMax.y >= bMin.y
                && aMin.z <= bMax.z && aMax.z >= bMin.z;
        }

        // 箱の中（表面をふくむ）で point に一番近い点。各軸で範囲に収めるだけでよい
        constexpr Vector3 ClosestPoint(const Vector3& point) const {
            const Vector3 min = Min();
            const Vector3 max = Max();
            return {
                Mathf::Clamp(point.x, min.x, max.x),
                Mathf::Clamp(point.y, min.y, max.y),
                Mathf::Clamp(point.z, min.z, max.z),
            };
        }

        // 箱までの距離の 2 乗（中なら 0）
        constexpr float SqrDistance(const Vector3& point) const { return (ClosestPoint(point) - point).SqrMagnitude(); }

        /// @brief レイと箱の交差（スラブ法）
        /// @param distance 箱に入るまでの距離。レイの始点が箱の中なら 0
        /// @return 前方で交わったら true
        bool IntersectRay(const Ray& ray, float& distance) const {
            // 各軸について「レイがその軸の範囲（スラブ）の中にいる区間 [t1, t2]」を求め、
            // 3 軸の区間がすべて重なる部分があれば、箱の中を通っている
            const Vector3 min = Min();
            const Vector3 max = Max();
            float tEnter = Mathf::NegativeInfinity;
            float tExit = Mathf::Infinity;
            for (int axis = 0; axis < 3; ++axis) {
                const float origin = ray.origin[axis];
                const float direction = ray.direction[axis];
                if (Mathf::Abs(direction) < 1e-8f) {
                    // この軸と平行。始点がスラブの外なら、ずっと外のまま
                    if (origin < min[axis] || origin > max[axis]) {
                        return false;
                    }
                    continue;
                }
                float t1 = (min[axis] - origin) / direction;
                float t2 = (max[axis] - origin) / direction;
                if (t1 > t2) {
                    const float temp = t1;
                    t1 = t2;
                    t2 = temp;
                }
                tEnter = Mathf::Max(tEnter, t1);
                tExit = Mathf::Min(tExit, t2);
                if (tEnter > tExit) {
                    return false;
                }
            }
            if (tExit < 0.0f) {
                return false;  // 箱はレイの後ろ
            }
            distance = Mathf::Max(tEnter, 0.0f);
            return true;
        }

        constexpr Bounds Transformed(const Matrix4x4& matrix) const {
            Bounds result{ matrix.MultiplyPoint3x4(center), Vector3{} };
            for (int col = 0; col < 3; ++col) {
                float extent = 0.0f;
                for (int row = 0; row < 3; ++row) {
                    extent += Mathf::Abs(matrix.m[row][col]) * extents[row];
                }
                result.extents[col] = extent;
            }
            return result;
        }
    };

}  // namespace TE