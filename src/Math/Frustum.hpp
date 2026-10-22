/**
 * \file Frustum.hpp
 * \brief 視錐台（カメラに写る範囲）
 */
#pragma once

#include "Math/BoundingSphere.hpp"
#include "Math/Plane.hpp"

namespace TE {

    // 6 枚の平面で囲まれた範囲。法線はすべて内側を向くので、全部の面の表側にあれば写っている。
    // Unity の GeometryUtility.CalculateFrustumPlanes / TestPlanesAABB に当たる
    struct Frustum {
        enum PlaneIndex { Left, Right, Bottom, Top, Near, Far, Count };
        Plane planes[Count];

        // View * Projection 行列から 6 枚の面を取り出す（Gribb-Hartmann の方法）。
        // クリップ座標で「-w ≤ x ≤ w、-w ≤ y ≤ w、0 ≤ z ≤ w」が写る範囲。
        // 行ベクトルなので、クリップ座標の x, y, z, w はそれぞれ行列の 0〜3 列目との内積になる
        static Frustum FromMatrix(const Matrix4x4& viewProjection) {
            const Vector4 x = viewProjection.GetColumn(0);
            const Vector4 y = viewProjection.GetColumn(1);
            const Vector4 z = viewProjection.GetColumn(2);
            const Vector4 w = viewProjection.GetColumn(3);

            Frustum frustum;
            frustum.planes[Left] = MakePlane(w + x);    // x ≥ -w
            frustum.planes[Right] = MakePlane(w - x);   // x ≤ w
            frustum.planes[Bottom] = MakePlane(w + y);  // y ≥ -w
            frustum.planes[Top] = MakePlane(w - y);     // y ≤ w
            frustum.planes[Near] = MakePlane(z);        // z ≥ 0（D3D の深度は 0 から）
            frustum.planes[Far] = MakePlane(w - z);     // z ≤ w
            return frustum;
        }

        bool Contains(const Vector3& point) const {
            for (const Plane& plane : planes) {
                if (plane.GetDistanceToPoint(point) < 0.0f) {
                    return false;
                }
            }
            return true;
        }

        // 少しでも写る可能性があれば true（正確な判定ではなく、写らないものを安く捨てるためのもの）
        bool Intersects(const Bounds& bounds) const {
            for (const Plane& plane : planes) {
                // 箱を法線方向に見たときの「半径」。これより遠く裏側にあれば、箱全体が面の外
                const float radius = bounds.extents.x * Mathf::Abs(plane.normal.x)
                    + bounds.extents.y * Mathf::Abs(plane.normal.y)
                    + bounds.extents.z * Mathf::Abs(plane.normal.z);
                if (plane.GetDistanceToPoint(bounds.center) < -radius) {
                    return false;
                }
            }
            return true;
        }

        bool Intersects(const BoundingSphere& sphere) const {
            for (const Plane& plane : planes) {
                if (plane.GetDistanceToPoint(sphere.position) < -sphere.radius) {
                    return false;
                }
            }
            return true;
        }

    private:
        // (a, b, c, d) を「ax + by + cz + d ≥ 0 が内側」の面にする。距離として使えるよう法線の長さで割る
        static Plane MakePlane(const Vector4& coefficients) {
            const Vector3 n = coefficients.XYZ();
            const float length = n.Magnitude();
            return { n / length, coefficients.w / length };
        }
    };

}  // namespace TE