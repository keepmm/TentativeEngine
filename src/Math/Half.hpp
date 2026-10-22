/**
 * \file Half.hpp
 * \brief 16 ビット浮動小数点（half）との変換
 */
#pragma once

#include <bit>
#include <cstdint>

namespace TE::Half {

    // float → half。一番近い値に丸める（ちょうど中間なら仮数が偶数の方。IEEE 754 の既定と同じ）
    inline std::uint16_t FromFloat(float value) {
        std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        const std::uint32_t sign = bits & 0x80000000u;
        bits ^= sign;  // ここから先は絶対値で考える

        std::uint32_t result = 0;
        if (bits >= 0x47800000u) {
            // 65536 以上（half では無限大）と、無限大・NaN
            result = bits > 0x7F800000u ? 0x7E00u : 0x7C00u;
        }
        else if (bits < 0x38800000u) {
            // 2⁻¹⁴ 未満は half の非正規化数（または 0）になる。
            // 0.5f を足すと、float の仮数の下の方に half の非正規化数の並びがそのまま入る（丸めも FPU がやってくれる）
            const float shifted = std::bit_cast<float>(bits) + 0.5f;
            result = std::bit_cast<std::uint32_t>(shifted) - 0x3F000000u;
        }
        else {
            // 正規化数: 指数のかさ上げ（127 → 15）を直して、仮数の下 13 ビットを切り捨てる。
            // 切る前に 0xFFF + (残す最下位ビット) を足すと、偶数への丸めになる
            const std::uint32_t mantissaOdd = (bits >> 13) & 1u;
            bits += (static_cast<std::uint32_t>(15 - 127) << 23) + 0xFFFu;
            bits += mantissaOdd;
            result = bits >> 13;
        }
        return static_cast<std::uint16_t>(result | (sign >> 16));
    }

    // half → float。float の方が広いので誤差なしで戻せる
    inline float ToFloat(std::uint16_t value) {
        constexpr std::uint32_t kShiftedExponent = 0x7C00u << 13;  // half の指数部を float の位置へずらしたもの
        std::uint32_t bits = (value & 0x7FFFu) << 13;              // 指数と仮数を float の位置へ
        const std::uint32_t exponent = bits & kShiftedExponent;
        bits += static_cast<std::uint32_t>(127 - 15) << 23;        // 指数のかさ上げを 15 → 127 に

        if (exponent == kShiftedExponent) {
            bits += static_cast<std::uint32_t>(128 - 16) << 23;    // 無限大・NaN は指数をすべて 1 に
        }
        else if (exponent == 0) {
            // 0 と非正規化数: 指数を 1 足して正規化数として作ってから、その分を引き算で戻す
            bits += 1u << 23;
            bits = std::bit_cast<std::uint32_t>(std::bit_cast<float>(bits) - std::bit_cast<float>(113u << 23));
        }
        bits |= static_cast<std::uint32_t>(value & 0x8000u) << 16;  // 符号
        return std::bit_cast<float>(bits);
    }

}  // namespace TE::Half