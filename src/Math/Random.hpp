/**
 * \file Random.hpp
 * \brief 乱数（Unity の Random 相当）
 */
#pragma once

#include "Math/Quaternion.hpp"
#include "Math/Vector2.hpp"

#include <cstdint>

namespace TE {

    // PCG32 という乱数生成器。小さく速く、偏りも少ない。
    // 同じシードなら必ず同じ並びになるので、リプレイやテストの再現に使える
    class RandomGenerator {
    public:
        explicit RandomGenerator(std::uint64_t seed = 0x853c49e6748fea9bULL, std::uint64_t sequence = 0xda3e39cb94b95bdbULL) {
            // 推奨されている初期化手順。sequence を変えると、同じシードでも別の並びになる
            m_Increment = (sequence << 1u) | 1u;  // 奇数でないと周期が短くなる
            NextUInt();
            m_State += seed;
            NextUInt();
        }

        // 0〜2³²-1 の一様な整数
        std::uint32_t NextUInt() {
            // 線形合同法で状態を進め、出力は状態のビットを混ぜて回転させる（そのまま出すと下位ビットの質が悪いため）
            const std::uint64_t old = m_State;
            m_State = old * 6364136223846793005ULL + m_Increment;
            const auto xorShifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
            const auto rotation = static_cast<std::uint32_t>(old >> 59u);
            return (xorShifted >> rotation) | (xorShifted << ((32u - rotation) & 31u));
        }

        // 0 以上 1 未満
        float Value() {
            // float の仮数部は 24 ビットなので、上位 24 ビットだけ使うとすべての値が同じ確率で出る
            return static_cast<float>(NextUInt() >> 8) * (1.0f / 16777216.0f);
        }

        // min 以上 max 未満の float
        float Range(float min, float max) { return min + (max - min) * Value(); }

        // min 以上 max 未満の int（Unity と同じく max はふくまない）
        int Range(int min, int max) {
            if (max <= min) {
                return min;
            }
            const auto range = static_cast<std::uint32_t>(static_cast<std::int64_t>(max) - min);
            // 単純に % range すると、2³² が range で割り切れないぶん小さい値が出やすくなる。
            // 余りの端数にあたる値（threshold 未満）は捨てて引き直す
            const std::uint32_t threshold = (0u - range) % range;
            for (;;) {
                const std::uint32_t r = NextUInt();
                if (r >= threshold) {
                    return static_cast<int>(static_cast<std::int64_t>(min) + r % range);
                }
            }
        }

        // 半径 1 の円の内側の一様な点
        Vector2 InsideUnitCircle() {
            // 正方形の中に点を打ち、円の外なら打ち直す（約 79% で一発で当たる）
            for (;;) {
                const Vector2 p{ Range(-1.0f, 1.0f), Range(-1.0f, 1.0f) };
                if (p.SqrMagnitude() <= 1.0f) {
                    return p;
                }
            }
        }

        // 半径 1 の球の内側の一様な点
        Vector3 InsideUnitSphere() {
            for (;;) {
                const Vector3 p{ Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f) };
                if (p.SqrMagnitude() <= 1.0f) {
                    return p;
                }
            }
        }

        // 半径 1 の球の表面の一様な点（ランダムな向き）
        Vector3 OnUnitSphere() {
            // 高さ z を一様に選ぶと、球の表面積も一様になる（アルキメデスの定理）
            const float z = Range(-1.0f, 1.0f);
            const float angle = Range(0.0f, 2.0f * Mathf::PI);
            const float radius = Mathf::Sqrt(Mathf::Max(0.0f, 1.0f - z * z));
            return { radius * Mathf::Cos(angle), radius * Mathf::Sin(angle), z };
        }

        // 一様にランダムな回転（Shoemake の方法）。オイラー角を一様に選ぶと、向きが極に偏る
        Quaternion Rotation() {
            const float u1 = Value();
            const float u2 = Value() * 2.0f * Mathf::PI;
            const float u3 = Value() * 2.0f * Mathf::PI;
            const float a = Mathf::Sqrt(1.0f - u1);
            const float b = Mathf::Sqrt(u1);
            return { a * Mathf::Sin(u2), a * Mathf::Cos(u2), b * Mathf::Sin(u3), b * Mathf::Cos(u3) };
        }

    private:
        std::uint64_t m_State = 0;
        std::uint64_t m_Increment = 0;
    };

    // Unity の Random.Range のように、どこからでも呼べる共通の乱数。
    // スレッドごとに別の生成器を持つので、ロックなしで呼べる
    namespace Random {

        inline RandomGenerator& Default() {
            thread_local RandomGenerator generator;
            return generator;
        }

        // シードを決める。既定のシードは固定なので、毎回違う並びにしたければ起動時に時刻などで呼ぶ
        inline void InitState(std::uint64_t seed) { Default() = RandomGenerator(seed); }

        inline float Value() { return Default().Value(); }
        inline float Range(float min, float max) { return Default().Range(min, max); }
        inline int Range(int min, int max) { return Default().Range(min, max); }
        inline Vector2 InsideUnitCircle() { return Default().InsideUnitCircle(); }
        inline Vector3 InsideUnitSphere() { return Default().InsideUnitSphere(); }
        inline Vector3 OnUnitSphere() { return Default().OnUnitSphere(); }
        inline Quaternion Rotation() { return Default().Rotation(); }

    }  // namespace Random

}  // namespace TE