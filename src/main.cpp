/**
 * \file main.cpp
 * \brief エントリポイント
 */
#include "Core//Defines.hpp"
#include "Engine/Engine.hpp"

#include <objbase.h>

#include <exception>

namespace
{
    /// @brief COMの初期化と後始末を対にする
    struct ComScope
    {
        ComScope()
        {
            THROW_IF_FAILED(CoInitializeEx(nullptr, COINITBASE_MULTITHREADED));
        }
        ~ComScope()
        {
            CoUninitialize();
        }
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;
    };
}

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int) {
    // 初期化の失敗は例外で上がってくる。黙って落ちないよう、ここで受けて内容を見せる
    try {
        ComScope com;
        Engine engine(instance);
        engine.Run();
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "TentativeEngine", MB_OK | MB_ICONERROR);
        return -1;
    }
    return 0;
}
