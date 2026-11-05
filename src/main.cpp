/**
 * \file main.cpp
 * \brief エントリポイント
 */
#include "Core/Platform.hpp"
#include "Engine/Engine.hpp"

#include <exception>

#if TE_PLATFORM_WINDOWS

#include "Core/Defines.hpp"
#include <objbase.h>

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

#else

#include "Core/Log.hpp"
#include "Platform/Paths.hpp"

#include <android/asset_manager.h>
#include <android_native_app_glue.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
    std::string ReadAsset(AAssetManager* manager, const std::string& name) {
        AAsset* asset = AAssetManager_open(manager, name.c_str(), AASSET_MODE_BUFFER);
        if (!asset) {
            return {};
        }
        std::string bytes(static_cast<std::size_t>(AAsset_getLength(asset)), '\0');
        AAsset_read(asset, bytes.data(), bytes.size());
        AAsset_close(asset);
        return bytes;
    }

    // APK の中は通常のパスで開けないので、最初に内部ストレージへ展開する。
    // こうすると assimp と ifstream は Windows と同じコードで読める。
    // AAssetManager はサブフォルダを列挙できないので、Gradle が作る一覧を使う
    void ExtractAssets(AAssetManager* manager, const std::filesystem::path& root) {
        std::istringstream manifest(ReadAsset(manager, "manifest.txt"));
        std::string name;
        while (std::getline(manifest, name)) {
            if (name.empty()) {
                continue;
            }
            const std::filesystem::path out = root / name;
            std::filesystem::create_directories(out.parent_path());
            const std::string bytes = ReadAsset(manager, name);
            std::ofstream(out, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
    }
}

void android_main(android_app* app) {
    try {
        const std::filesystem::path root = app->activity->internalDataPath;
        TE::AndroidBaseDirectory() = root;
        ExtractAssets(app->activity->assetManager, root);
        Engine engine(app);
        engine.Run();
    } catch (const std::exception& e) {
        LogError(e.what());
    }
}

#endif
