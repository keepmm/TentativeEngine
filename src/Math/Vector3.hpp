/**
 * \file Vector3.hpp
 * \brief 3 次元ベクトル（Unity の Vector3 相当）
 */
#pragma once

#include "Math/Mathf.hpp"

namespace TE {

    // 左手系。X が右、Y が上、Z が前（Unity と同じ）
    struct Vector3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        // ---- 定数（定義はクラスの外。自分の型の constexpr 定数はクラスの中では作れないため）---- //
        static const Vector3 zero;
        static const Vector3 one;
        static const Vector3 up;
        static const Vector3 down;
        static const Vector3 left;
        static const Vector3 right;
        static const Vector3 forward;
        static const Vector3 back;

        // ---- 演算子（hidden friend にしておくと、メンバ関数の中からも使える）---- //
        friend constexpr Vector3 operator+(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        friend constexpr Vector3 operator-(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        friend constexpr Vector3 operator-(const Vector3& v) { return { -v.x, -v.y, -v.z }; }
        friend constexpr Vector3 operator*(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
        friend constexpr Vector3 operator*(float s, const Vector3& v) { return v * s; }
        friend constexpr Vector3 operator/(const Vector3& v, float s) { return { v.x / s, v.y / s, v.z / s }; }
        friend constexpr Vector3& operator+=(Vector3& a, const Vector3& b) { return a = a + b; }
        friend constexpr Vector3& operator-=(Vector3& a, const Vector3& b) { return a = a - b; }
        friend constexpr Vector3& operator*=(Vector3& v, float s) { return v = v * s; }
        friend constexpr Vector3& operator/=(Vector3& v, float s) { return v = v / s; }

        // 軸の番号（0: x, 1: y, 2: z）で読み書きする。AABB の計算などで軸ごとにループしたいとき用
        constexpr float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
        constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

        // ---- 長さ ---- //
        // 比べるだけなら √ のいらない SqrMagnitude を使う
        constexpr float SqrMagnitude() const { return Dot(*this, *this); }
        float Magnitude() const { return Mathf::Sqrt(SqrMagnitude()); }

        // 長さがほぼ 0 なら 0 ベクトルを返す（0 で割って NaN が広がるのを防ぐ）
        Vector3 Normalized() const {
            const float magnitude = Magnitude();
            return magnitude > 1e-5f ? *this / magnitude : Vector3{};
        }
        void Normalize() { *this = Normalized(); }

        // ---- static 関数 ---- //

        // |a||b|cosθ。両方が長さ 1 なら cosθ（1: 同じ向き、0: 直角、-1: 逆向き）
        static constexpr float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

        // a と b の両方に垂直なベクトル。左手系なので Cross(right, up) == forward
        static constexpr Vector3 Cross(const Vector3& a, const Vector3& b) {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x,
            };
        }

        static float Distance(const Vector3& a, const Vector3& b) { return (a - b).Magnitude(); }

        // 成分ごとの掛け算（Unity と同じく演算子にはしない。内積と取り違えないため）
        static constexpr Vector3 Scale(const Vector3& a, const Vector3& b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
        static constexpr Vector3 Min(const Vector3& a, const Vector3& b) {
            return { Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y), Mathf::Min(a.z, b.z) };
        }
        static constexpr Vector3 Max(const Vector3& a, const Vector3& b) {
            return { Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y), Mathf::Max(a.z, b.z) };
        }

        static constexpr Vector3 LerpUnclamped(const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }
        static constexpr Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
            return LerpUnclamped(a, b, Mathf::Clamp01(t));
        }

        // 長さを maxLength までに抑える（速度の上限など）
        static Vector3 ClampMagnitude(const Vector3& v, float maxLength) {
            const float sqr = v.SqrMagnitude();
            if (sqr <= maxLength * maxLength) {
                return v;
            }
            return v / Mathf::Sqrt(sqr) * maxLength;
        }

        // 2 つのベクトルのなす角（度、0〜180）
        static float Angle(const Vector3& from, const Vector3& to) {
            const float denominator = Mathf::Sqrt(from.SqrMagnitude() * to.SqrMagnitude());
            if (denominator < 1e-15f) {
                return 0.0f;
            }
            // 誤差で 1 をわずかに超えると acos が NaN になるので、範囲に収めてから渡す
            const float cosine = Mathf::Clamp(Dot(from, to) / denominator, -1.0f, 1.0f);
            return Mathf::Acos(cosine) * Mathf::Rad2Deg;
        }

        // axis のまわりに見たときの符号付きの角度（度、-180〜180）。axis の + 側から見て時計回りが正
        static float SignedAngle(const Vector3& from, const Vector3& to, const Vector3& axis) {
            // 外積が axis と同じ向きなら正の回転（左手系なので時計回り）
            return Angle(from, to) * Mathf::Sign(Dot(axis, Cross(from, to)));
        }

        // onNormal 方向の成分だけを取り出す
        static Vector3 Project(const Vector3& v, const Vector3& onNormal) {
            const float sqr = onNormal.SqrMagnitude();
            if (sqr < 1e-15f) {
                return Vector3{};
            }
            return onNormal * (Dot(v, onNormal) / sqr);
        }

        // 面に沿った成分だけを残す（坂道に沿って歩かせるなど）
        static Vector3 ProjectOnPlane(const Vector3& v, const Vector3& planeNormal) {
            return v - Project(v, planeNormal);
        }

        // 面で跳ね返った向き。inNormal は長さ 1 のもの
        static constexpr Vector3 Reflect(const Vector3& inDirection, const Vector3& inNormal) {
            return inDirection - inNormal * (2.0f * Dot(inDirection, inNormal));
        }

        // target へ向かって最大 maxDistanceDelta だけ進む。行き過ぎない
        static Vector3 MoveTowards(const Vector3& current, const Vector3& target, float maxDistanceDelta) {
            const Vector3 toTarget = target - current;
            const float distance = toTarget.Magnitude();
            if (distance <= maxDistanceDelta || distance == 0.0f) {
                return target;
            }
            return current + toTarget / distance * maxDistanceDelta;
        }

        /// @brief バネのように滑らかに target へ近づける（Mathf::SmoothDamp のベクトル版）
        /// @param deltaTime Unity と違い省略できない。数学ライブラリは Time（エンジン側）を知らないため
        static Vector3 SmoothDamp(const Vector3& current, const Vector3& target, Vector3& currentVelocity,
            float smoothTime, float deltaTime, float maxSpeed = Mathf::Infinity) {
            smoothTime = Mathf::Max(0.0001f, smoothTime);
            const float omega = 2.0f / smoothTime;
            const float x = omega * deltaTime;
            const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

            // 速度の上限は軸ごとではなく、ベクトルの長さで抑える
            const Vector3 change = ClampMagnitude(current - target, maxSpeed * smoothTime);
            const Vector3 clampedTarget = current - change;

            const Vector3 temp = (currentVelocity + change * omega) * deltaTime;
            currentVelocity = (currentVelocity - temp * omega) * decay;
            Vector3 output = clampedTarget + (change + temp) * decay;

            // 本来の目標を通り過ぎたら（目標の向こう側に出たら）、そこで止める
            if (Dot(target - current, output - target) > 0.0f) {
                output = target;
                currentVelocity = Vector3{};
            }
            return output;
        }
    };

    inline constexpr Vector3 Vector3::zero{ 0.0f, 0.0f, 0.0f };
    inline constexpr Vector3 Vector3::one{ 1.0f, 1.0f, 1.0f };
    inline constexpr Vector3 Vector3::up{ 0.0f, 1.0f, 0.0f };
    inline constexpr Vector3 Vector3::down{ 0.0f, -1.0f, 0.0f };
    inline constexpr Vector3 Vector3::left{ -1.0f, 0.0f, 0.0f };
    inline constexpr Vector3 Vector3::right{ 1.0f, 0.0f, 0.0f };
    inline constexpr Vector3 Vector3::forward{ 0.0f, 0.0f, 1.0f };
    inline constexpr Vector3 Vector3::back{ 0.0f, 0.0f, -1.0f };

}  // namespace TE