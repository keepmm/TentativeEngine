/**
 * \file Defines.hpp
 * \brief エンジン全体で使う共通の型・マクロ・エラー処理
 */
#pragma once

#include <Windows.h>
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>

// COM オブジェクトのスマートポインタ。スコープを抜けると Release が呼ばれる。
// 生ポインタで持つと Release の漏れや二重解放が起きやすいので、COM 系は必ずこれを使う。
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// 失敗した HRESULT を例外に変換する。
// 呼び出し側で毎回 if を書かなくても、初期化の途中で失敗したらそこで抜けられる。
// file / line を残すのは、どの API 呼び出しで落ちたかを後から特定するため。
inline void ThrowIfFailed(HRESULT hr, const char* file, int line) {
    if (FAILED(hr)) {
        char message[256];
        std::snprintf(message, sizeof(message), "HRESULT 0x%08X\n%s (line %d)",
                      static_cast<unsigned>(hr), file, line);
        throw std::runtime_error(message);
    }
}

// HRESULT を返す呼び出しを包む。__FILE__ / __LINE__ はマクロを書いた位置に展開される
#define THROW_IF_FAILED(hr) ThrowIfFailed((hr), __FILE__, __LINE__)
