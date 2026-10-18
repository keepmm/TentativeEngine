/**
 * \file Timer.hpp
 * \brief 経過時間を測るタイマー
 */
#pragma once

#include <Windows.h>

// QueryPerformanceCounter（高精度カウンタ）で時間を測る。
// timeGetTime などより分解能が細かく、フレーム単位の計測に向く。
class Timer {
public:
    Timer() {
        QueryPerformanceFrequency(&m_Frequency);
        QueryPerformanceCounter(&m_Last);
    }

    // 前回 Tick を呼んだときからの経過秒数を返す。毎フレーム 1 回呼ぶ想定
    double Tick() {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double seconds = static_cast<double>(now.QuadPart - m_Last.QuadPart)
                             / static_cast<double>(m_Frequency.QuadPart);
        m_Last = now;
        return seconds;
    }

private:
    LARGE_INTEGER m_Frequency{};
    LARGE_INTEGER m_Last{};
};
