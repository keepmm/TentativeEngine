/**
 * \file Timer.hpp
 * \brief 経過時間を測るタイマー
 */
#pragma once

#include <chrono>


class Timer {
public:
    Timer() : m_Last(Clock::now()) {}

    /// @return 前回Tickを呼んだ時からの経過秒数を返す 
    double Tick()
    {
        const Clock::time_point now = Clock::now();
        const std::chrono::duration<double> seconds = now - m_Last;
        m_Last = now;
        return seconds.count();
    }
private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_Last;
};
