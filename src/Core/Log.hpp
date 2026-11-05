/**
 * \file Log.hpp
 * \brief ログ出力
 */
#pragma once

#include "Core/Platform.hpp"

#if TE_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <android/log.h>
#endif

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

enum class LogLevel : std::uint8_t {
    Info,
    Warning,
    Error,
};

// 出力ウィンドウのほかに流す先（エディターの Console など）。1 つだけ差し込める。
// 呼び出し側のスレッドでそのまま呼ばれるので、受け取る側で必要なら排他する
using LogSink = std::function<void(LogLevel level, const std::string& message)>;

namespace LogDetail {
    inline LogSink g_Sink;
}

// nullptr で外す。差し込んだ側が先に消えるときは、消える前に必ず外すこと
inline void SetLogSink(LogSink sink) { LogDetail::g_Sink = std::move(sink); }

// Visual Studio の出力ウィンドウにメッセージを流す。
// デバッガを付けて実行すれば見える。ファイルへの保存は必要になってから追加する。
inline void Log(const std::string& message) {
#if TE_PLATFORM_WINDOWS
    OutputDebugStringA((message + "\n").c_str());
#else
    __android_log_write(ANDROID_LOG_INFO, "TentativeEngine",message.c_str());
#endif
}

// 重要度つき。出力ウィンドウで [ERROR] を検索すれば失敗だけ拾える
inline void Log(LogLevel level, const std::string& message) {
    static constexpr const char* kPrefixes[] = { "[INFO] ", "[WARN] ", "[ERROR] " };
    Log(kPrefixes[static_cast<std::size_t>(level)] + message);
    if (LogDetail::g_Sink) {
        LogDetail::g_Sink(level, message);
    }
}

inline void LogInfo(const std::string& message) { Log(LogLevel::Info, message); }
inline void LogWarning(const std::string& message) { Log(LogLevel::Warning, message); }
inline void LogError(const std::string& message) { Log(LogLevel::Error, message); }
