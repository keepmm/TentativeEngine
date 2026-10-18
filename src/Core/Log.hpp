/**
 * \file Log.hpp
 * \brief ログ出力
 */
#pragma once

#include <Windows.h>

#include <string>

// Visual Studio の出力ウィンドウにメッセージを流す。
// デバッガを付けて実行すれば見える。ファイルへの保存は必要になってから追加する。
inline void Log(const std::string& message) {
    OutputDebugStringA((message + "\n").c_str());
}

// 重要度つき。出力ウィンドウで [ERROR] を検索すれば失敗だけ拾える
inline void LogInfo(const std::string& message) { Log("[INFO] " + message); }
inline void LogWarning(const std::string& message) { Log("[WARN] " + message); }
inline void LogError(const std::string& message) { Log("[ERROR] " + message); }
