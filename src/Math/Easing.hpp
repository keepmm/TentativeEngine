/**
 * \file Easing.hpp
 * \brief イージング関数（UI アニメーションやカメラ演出の動きの緩急）
 */
#pragma once

#include "Math/Mathf.hpp"

 // どれも t = 0 で 0、t = 1 で 1 を返す。Mathf::LerpUnclamped(a, b, Easing::OutQuad(t)) のように使う。
 // In = ゆっくり始まる、Out = ゆっくり終わる、InOut = 両方
namespace TE::Easing {

    constexpr float Linear(float t) { return t; }

    // ---- 2 次・3 次（ほどよい緩急）---- //
    constexpr float InQuad(float t) { return t * t; }
    constexpr float OutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
    constexpr float InOutQuad(float t) {
        if (t < 0.5f) {
            return 2.0f * t * t;
        }
        const float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * 0.5f;
    }

    constexpr float InCubic(float t) { return t * t * t; }
    constexpr float OutCubic(float t) {
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    constexpr float InOutCubic(float t) {
        if (t < 0.5f) {
            return 4.0f * t * t * t;
        }
        const float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * u * 0.5f;
    }

    // ---- sin（一番やわらかい）---- //
    inline float InSine(float t) { return 1.0f - Mathf::Cos(t * Mathf::PI * 0.5f); }
    inline float OutSine(float t) { return Mathf::Sin(t * Mathf::PI * 0.5f); }
    inline float InOutSine(float t) { return -(Mathf::Cos(Mathf::PI * t) - 1.0f) * 0.5f; }

    // ---- 指数（急な緩急）---- //
    inline float InExpo(float t) { return t <= 0.0f ? 0.0f : Mathf::Pow(2.0f, 10.0f * t - 10.0f); }
    inline float OutExpo(float t) { return t >= 1.0f ? 1.0f : 1.0f - Mathf::Pow(2.0f, -10.0f * t); }

    // ---- Back（いったん逆へ引いてから進む / 行き過ぎて戻る）---- //
    inline constexpr float kBackOvershoot = 1.70158f;  // 約 10% 行き過ぎる定番の値

    constexpr float InBack(float t) {
        const float c3 = kBackOvershoot + 1.0f;
        return c3 * t * t * t - kBackOvershoot * t * t;
    }
    constexpr float OutBack(float t) {
        const float c3 = kBackOvershoot + 1.0f;
        const float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + kBackOvershoot * u * u;
    }

    // ---- Elastic（ばねのように揺れて止まる）---- //
    inline float OutElastic(float t) {
        if (t <= 0.0f) {
            return 0.0f;
        }
        if (t >= 1.0f) {
            return 1.0f;
        }
        const float period = 2.0f * Mathf::PI / 3.0f;
        return Mathf::Pow(2.0f, -10.0f * t) * Mathf::Sin((t * 10.0f - 0.75f) * period) + 1.0f;
    }

    // ---- Bounce（床で弾むように止まる）---- //
    constexpr float OutBounce(float t) {
        // 放物線を 4 つつないで、だんだん小さくなる弾みを作る
        constexpr float n = 7.5625f;
        constexpr float d = 2.75f;
        if (t < 1.0f / d) {
            return n * t * t;
        }
        if (t < 2.0f / d) {
            t -= 1.5f / d;
            return n * t * t + 0.75f;
        }
        if (t < 2.5f / d) {
            t -= 2.25f / d;
            return n * t * t + 0.9375f;
        }
        t -= 2.625f / d;
        return n * t * t + 0.984375f;
    }
    constexpr float InBounce(float t) { return 1.0f - OutBounce(1.0f - t); }

}  // namespace TE::Easing