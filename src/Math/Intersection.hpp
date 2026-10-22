/**
 * \file Intersection.hpp
 * \brief 形どうしの交差判定のうち、どの型のメンバにもしにくいもの
 */
#pragma once

#include "Math/Ray.hpp"

namespace TE::Intersection {

/// @brief レイと三角形の交差（Möller–Trumbore 法）。メッシュをポリゴン単位で選ぶときに使う
/// @param distance 交点までの距離
/// @param u, v 交点の重心座標。交点 = a + (b - a) u + (c - a) v（UV や法線の補間に使う）
/// @return 前方で交わったら true。表と裏の両方で当たる
inline bool RayTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c,
                        float& distance, float& u, float& v) {
    // o + d t = a + e1 u + e2 v を、クラメルの公式で t, u, v について解く
    const Vector3 edge1 = b - a;
    const Vector3 edge2 = c - a;
    const Vector3 p = Vector3::Cross(ray.direction, edge2);
    const float determinant = Vector3::Dot(edge1, p);
    if (Mathf::Abs(determinant) < 1e-8f) {
        return false;  // レイが三角形の面と平行
    }
    const float inverse = 1.0f / determinant;

    const Vector3 s = ray.origin - a;
    u = Vector3::Dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const Vector3 q = Vector3::Cross(s, edge1);
    v = Vector3::Dot(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f) {
        return false;  // 重心座標が三角形の外
    }
    distance = Vector3::Dot(edge2, q) * inverse;
    return distance > 0.0f;
}

inline bool RayTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c, float& distance) {
    float u = 0.0f;
    float v = 0.0f;
    return RayTriangle(ray, a, b, c, distance, u, v);
}

// 線分 a-b の上で point に一番近い点（カプセルの当たり判定などで使う）
inline Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& a, const Vector3& b) {
    const Vector3 ab = b - a;
    const float sqr = ab.SqrMagnitude();
    if (sqr < 1e-15f) {
        return a;  // 線分の長さが 0
    }
    // 直線に下ろした足の位置を 0〜1 に収めると、線分の端を越えなくなる
    const float t = Mathf::Clamp01(Vector3::Dot(point - a, ab) / sqr);
    return a + ab * t;
}

}  // namespace TE::Intersection
