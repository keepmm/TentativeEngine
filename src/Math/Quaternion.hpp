/**
 * \file Quaternion.hpp
 * \brief 回転（Unity の Quaternion 相当）
 */
#pragma once

#include "Math/Vector3.hpp"

namespace TE {
    struct Quaternion {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;  // 既定値は「回転なし」

        static const Quaternion identity;

        // ---- 演算子 ---- //

        friend constexpr Quaternion operator*(const Quaternion& a, const Quaternion& b) {
            return {
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            };
        }

        friend Vector3 operator*(const Quaternion& q, const Vector3& v) {
            const Vector3 u{ q.x, q.y, q.z };
            const Vector3 t = Vector3::Cross(u, v) * 2.0f;
            return v + t * q.w + Vector3::Cross(u, t);
        }

        // ---- メンバ関数 ---- //

        // 掛け算を繰り返すと誤差で長さが 1 からずれ、回転に拡大が混ざるので、ときどき戻す
        Quaternion Normalized() const {
            const float magnitude = Mathf::Sqrt(Dot(*this, *this));
            if (magnitude < 1e-15f) {
                return Quaternion{};
            }
            return { x / magnitude, y / magnitude, z / magnitude, w / magnitude };
        }
        void Normalize() { *this = Normalized(); }

        // Euler の逆。各成分 0〜360 度で返す（Unity の eulerAngles と同じ）
        Vector3 EulerAngles() const {
            const float m02 = 2.0f * (x * z + w * y);
            const float m10 = 2.0f * (x * y + w * z);
            const float m11 = 1.0f - 2.0f * (x * x + z * z);
            const float m12 = 2.0f * (y * z - w * x);
            const float m22 = 1.0f - 2.0f * (x * x + y * y);

            const float sinX = -m12;
            const float cosX = Mathf::Sqrt(m10 * m10 + m11 * m11);
            const float angleX = Mathf::Atan2(sinX, cosX);
            float angleY = 0.0f;
            float angleZ = 0.0f;
            if (cosX > 1e-3f) {
                angleY = Mathf::Atan2(m02, m22);  // = sinY cosX / cosY cosX
                angleZ = Mathf::Atan2(m10, m11);  // = cosX sinZ / cosX cosZ
            }
            else {
                const float m00 = 1.0f - 2.0f * (y * y + z * z);
                const float m20 = 2.0f * (x * z - w * y);
                angleY = Mathf::Atan2(-m20, m00);
            }
            return {
                Mathf::Repeat(angleX * Mathf::Rad2Deg, 360.0f),
                Mathf::Repeat(angleY * Mathf::Rad2Deg, 360.0f),
                Mathf::Repeat(angleZ * Mathf::Rad2Deg, 360.0f),
            };
        }

        // ---- 作る ---- //

        // axis まわりに angle 度回す
        static Quaternion AngleAxis(float angle, const Vector3& axis) {
            const Vector3 n = axis.Normalized();
            if (n.SqrMagnitude() == 0.0f) {
                return Quaternion{};
            }
            const float half = angle * Mathf::Deg2Rad * 0.5f;
            const float s = Mathf::Sin(half);
            return { n.x * s, n.y * s, n.z * s, Mathf::Cos(half) };
        }

        static Quaternion Euler(float x, float y, float z) {
            return AngleAxis(y, Vector3::up) * AngleAxis(x, Vector3::right) * AngleAxis(z, Vector3::forward);
        }
        static Quaternion Euler(const Vector3& euler) { return Euler(euler.x, euler.y, euler.z); }

        // from の向きを to の向きへ回す、最短の回転
        static Quaternion FromToRotation(const Vector3& from, const Vector3& to) {
            const Vector3 a = from.Normalized();
            const Vector3 b = to.Normalized();
            const float cosine = Mathf::Clamp(Vector3::Dot(a, b), -1.0f, 1.0f);
            if (cosine > 0.999999f) {
                return Quaternion{};
            }
            if (cosine < -0.999999f) {
                // 真逆のときは回転軸が決まらない（外積が 0）。a に垂直な軸ならどれでも 180 度で届く
                Vector3 axis = Vector3::Cross(Vector3::right, a);
                if (axis.SqrMagnitude() < 1e-6f) {
                    axis = Vector3::Cross(Vector3::up, a);
                }
                return AngleAxis(180.0f, axis);
            }
            return AngleAxis(Mathf::Acos(cosine) * Mathf::Rad2Deg, Vector3::Cross(a, b));
        }

        // Z 軸（前）を forward に、Y 軸（上）をできるだけ upwards に向ける回転
        static Quaternion LookRotation(const Vector3& forward, const Vector3& upwards = Vector3::up) {
            const Vector3 f = forward.Normalized();
            if (f.SqrMagnitude() == 0.0f) {
                return Quaternion{};
            }
            Vector3 r = Vector3::Cross(upwards, f);
            if (r.SqrMagnitude() < 1e-12f) {
                // 前と上が平行で右が決まらない。上は気にせず、前だけ合わせる
                return FromToRotation(Vector3::forward, f);
            }
            r.Normalize();
            const Vector3 u = Vector3::Cross(f, r);

            // 回転後の X 軸 = r、Y 軸 = u、Z 軸 = f。この 3 軸を並べた回転行列からクォータニオンを取り出す。
            // √ の中が小さいと誤差が増えるので、対角のうち一番大きいものを使う式を選ぶ
            const float trace = r.x + u.y + f.z;
            if (trace > 0.0f) {
                const float s = Mathf::Sqrt(trace + 1.0f) * 2.0f;  // = 4w
                return { (u.z - f.y) / s, (f.x - r.z) / s, (r.y - u.x) / s, 0.25f * s };
            }
            if (r.x > u.y && r.x > f.z) {
                const float s = Mathf::Sqrt(1.0f + r.x - u.y - f.z) * 2.0f;  // = 4x
                return { 0.25f * s, (u.x + r.y) / s, (f.x + r.z) / s, (u.z - f.y) / s };
            }
            if (u.y > f.z) {
                const float s = Mathf::Sqrt(1.0f + u.y - r.x - f.z) * 2.0f;  // = 4y
                return { (u.x + r.y) / s, 0.25f * s, (f.y + u.z) / s, (f.x - r.z) / s };
            }
            const float s = Mathf::Sqrt(1.0f + f.z - r.x - u.y) * 2.0f;  // = 4z
            return { (f.x + r.z) / s, (f.y + u.z) / s, 0.25f * s, (r.y - u.x) / s };
        }

        // ---- 計算 ---- //

        static constexpr float Dot(const Quaternion& a, const Quaternion& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        }

        // 逆回転。長さ 1 なら共役（軸を反転）と同じ
        static Quaternion Inverse(const Quaternion& q) {
            const float sqr = Dot(q, q);
            if (sqr < 1e-15f) {
                return Quaternion{};
            }
            return { -q.x / sqr, -q.y / sqr, -q.z / sqr, q.w / sqr };
        }

        // 2 つの回転の差の角度（度、0〜180）
        static float Angle(const Quaternion& a, const Quaternion& b) {
            // 差の回転 d = a⁻¹ * b の角度。acos(w) は w が 1 に近い（小さな角度の）ときに精度が落ちるので、
            // sin と cos の両方から atan2 で求める。q と -q は同じ回転なので w の符号は見ない
            const Quaternion d = Inverse(a) * b;
            const float sinHalf = Mathf::Sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            return Mathf::Atan2(sinHalf, Mathf::Abs(d.w)) * 2.0f * Mathf::Rad2Deg;
        }

        // 球面線形補間。回転の速さが一定になる
        static Quaternion SlerpUnclamped(const Quaternion& a, const Quaternion& b, float t) {
            float cosine = Dot(a, b);
            Quaternion end = b;
            // q と -q は同じ回転。内積が負なら反転して、近い方の道を通る
            if (cosine < 0.0f) {
                end = { -b.x, -b.y, -b.z, -b.w };
                cosine = -cosine;
            }

            float weightA = 1.0f - t;
            float weightB = t;
            // ほぼ同じ向きだと sin が 0 に近く割り算が不安定なので、線形補間のままにする
            if (cosine < 0.9995f) {
                const float theta = Mathf::Acos(cosine);
                const float sinTheta = Mathf::Sin(theta);
                weightA = Mathf::Sin((1.0f - t) * theta) / sinTheta;
                weightB = Mathf::Sin(t * theta) / sinTheta;
            }
            const Quaternion result{
                a.x * weightA + end.x * weightB,
                a.y * weightA + end.y * weightB,
                a.z * weightA + end.z * weightB,
                a.w * weightA + end.w * weightB,
            };
            return result.Normalized();
        }
        static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
            return SlerpUnclamped(a, b, Mathf::Clamp01(t));
        }

        // to へ向かって最大 maxDegreesDelta 度だけ回す。行き過ぎない
        static Quaternion RotateTowards(const Quaternion& from, const Quaternion& to, float maxDegreesDelta) {
            const float angle = Angle(from, to);
            if (angle == 0.0f) {
                return to;
            }
            return SlerpUnclamped(from, to, Mathf::Min(1.0f, maxDegreesDelta / angle));
        }
    };

    inline constexpr Quaternion Quaternion::identity{ 0.0f, 0.0f, 0.0f, 1.0f };

}  // namespace TE