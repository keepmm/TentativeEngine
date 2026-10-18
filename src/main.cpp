/**
 * \file main.cpp
 * \brief エントリポイント
 */
#include "Engine/Engine.hpp"

#include <exception>

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int) {
    // 初期化の失敗は例外で上がってくる。黙って落ちないよう、ここで受けて内容を見せる
    try {
        Engine engine(instance);
        engine.Run();
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "TentativeEngine", MB_OK | MB_ICONERROR);
        return -1;
    }
    return 0;
}
