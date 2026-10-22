/*****************************************************************//**
 * \file   Color.hpp
 * \brief  色
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "Vector4.hpp"

#include <cstdint>

namespace TE
{
	struct Color
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;

        static const Color white;
        static const Color black;
        static const Color red;
        static const Color green;
        static const Color blue;
        static const Color yellow;
        static const Color cyan;
        static const Color magenta;
        static const Color gray;
        static const Color clear;

        friend constexpr Color operator+(const Color& x, const Color& y) { return { x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a }; }
        friend constexpr Color operator-(const Color& x, const Color& y) { return { x.r - y.r, x.g - y.g, x.b - y.b, x.a - y.a }; }
        friend constexpr Color operator*(const Color& x, const Color& y) { return { x.r * y.r, x.g * y.g, x.b * y.b, x.a * y.a }; }
        friend constexpr Color operator*(const Color& c, float s) { return { c.r * s, c.g * s, c.b * s, c.a * s }; }
        friend constexpr Color operator*(float s, const Color& c) { return c * s; }
        friend constexpr Color operator/(const Color& c, float s) { return { c.r / s, c.g / s, c.b / s, c.a / s }; }

        constexpr Vector4 ToVector4() const { return { r, g, b, a }; }
        static constexpr Color FromVector4(const Vector4& v) { return { v.x, v.y, v.z, v.w }; }

        /// @brief ガンマ空間の値を線形空間の値に変換します。
        /// @param value 変換するガンマ空間の値。
        /// @return 線形空間に変換された値。
        static float GammaToLinearSpace(float value)
        {
            return value <= 0.04045f ? value / 12.92f : Mathf::Pow((value + 0.055f) / 1.055f, 2.4f);
        }

        static float LinearToGammaSpace(float value)
        {
            return value <= 0.0031308f ? value * 12.92f : 1.055f * Mathf::Pow(value, 1.0f / 2.4f) - 0.055f;
        }

        // a は明るさではないので変換しない
        Color Linear() const { return { GammaToLinearSpace(r), GammaToLinearSpace(g), GammaToLinearSpace(b), a }; }
        Color Gamma() const { return { LinearToGammaSpace(r), LinearToGammaSpace(g), LinearToGammaSpace(b), a }; }

        constexpr float Grayscale() const { return 0.299f * r + 0.587f * g + 0.114f * b; }
        constexpr float MaxColorComponent() const { return Mathf::Max(Mathf::Max(r, g), b); }

        static constexpr Color LerpUnclamped(const Color& x, const Color& y, float t) { return x + (y - x) * t; }
        static constexpr Color Lerp(const Color& x, const Color& y, float t) { return LerpUnclamped(x, y, Mathf::Clamp01(t)); }

        static Color HSVToRGB(float h, float s, float v)
        {
            if (s <= 0.0f)
            {
                return{
                    v,v,v,1.0f
                };
            }

            // 色相環を 6つの区間に分け、区間ごとにRGBのどれが増減するか決める
            const float h6 = Mathf::Repeat(h, 1.0f) * 6.0f;
            const int sector = Mathf::Min(Mathf::FloorToInt(h6), 5);
            const float f = h6 - static_cast<float>(sector);
            const float p = v * (1.0f - s);
            const float q = v * (1.0f - s * f);
            const float t = v * (1.0f - s * (1.0f - f));
            switch (sector)
            {
            case 0: return { v, t, p, 1.0f };
            case 1: return { q, v, p, 1.0f };
            case 2: return { p, v, t, 1.0f };
            case 3: return { p, q, v, 1.0f };
            case 4: return { t, p, v, 1.0f };
            default: return { v, p, q, 1.0f };
            }
        }

        static void RGBToHSV(const Color& color, float& h, float& s, float& v) {
            const float max = color.MaxColorComponent();
            const float min = Mathf::Min(Mathf::Min(color.r, color.g), color.b);
            const float delta = max - min;
            v = max;
            s = max > 0.0f ? delta / max : 0.0f;
            if (delta <= 0.0f) {
                h = 0.0f;
                return;
            }
            // 一番強い成分がどれかで、色相環のどの区間にいるかがわかる
            if (max == color.r) {
                h = (color.g - color.b) / delta;
            }
            else if (max == color.g) {
                h = (color.b - color.r) / delta + 2.0f;
            }
            else {
                h = (color.r - color.g) / delta + 4.0f;
            }
            h = Mathf::Repeat(h / 6.0f, 1.0f);
        }
	};
    inline constexpr Color Color::white{ 1.0f, 1.0f, 1.0f, 1.0f };
    inline constexpr Color Color::black{ 0.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr Color Color::red{ 1.0f, 0.0f, 0.0f, 1.0f };
    inline constexpr Color Color::green{ 0.0f, 1.0f, 0.0f, 1.0f };
    inline constexpr Color Color::blue{ 0.0f, 0.0f, 1.0f, 1.0f };
    inline constexpr Color Color::yellow{ 1.0f, 0.92f, 0.016f, 1.0f };  // Unity と同じ、少し落ち着いた黄色
    inline constexpr Color Color::cyan{ 0.0f, 1.0f, 1.0f, 1.0f };
    inline constexpr Color Color::magenta{ 1.0f, 0.0f, 1.0f, 1.0f };
    inline constexpr Color Color::gray{ 0.5f, 0.5f, 0.5f, 1.0f };
    inline constexpr Color Color::clear{ 0.0f, 0.0f, 0.0f, 0.0f };

    // 各成分 0〜255 の 8 ビット。テクスチャのピクセルや頂点カラーなど、メモリを節約したいとき用
    struct Color32 {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;

        // 0〜1 に収めてから 255 倍して四捨五入する
        static Color32 FromColor(const Color& c) {
            const auto toByte = [](float value) {
                return static_cast<std::uint8_t>(Mathf::RoundToInt(Mathf::Clamp01(value) * 255.0f));
                };
            return { toByte(c.r), toByte(c.g), toByte(c.b), toByte(c.a) };
        }

        constexpr Color ToColor() const {
            return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
        }

        friend constexpr bool operator==(const Color32& x, const Color32& y) {
            return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
        }
        friend constexpr bool operator!=(const Color32& x, const Color32& y) { return !(x == y); }
    };

}  // namespace TE
