/*****************************************************************//**
 * \file   Paths.hpp
 * \brief  exe の場所を基準としたフォルダの探し方
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once
#include "Core/Platform.hpp"
#if TE_PLATFORM_WINDOWS
#include <Windows.h>
#endif

#include <filesystem>
#include <string>
#include <system_error>

namespace TE
{
#if TE_PLATFORM_ANDROID
	// android_main が内部ストレージの場所を入れる
	inline std::filesystem::path& AndroidBaseDirectory()
	{
		static std::filesystem::path directory;
		return directory;
	}
	inline std::filesystem::path ExecutableDirectory() { return AndroidBaseDirectory(); }
#else
	inline std::filesystem::path ExecutableDirectory()
	{
		// MAX_PATHを超える場所に置かれても取れるように
		// 足りなければ広げて取り直す
		std::wstring buffer(MAX_PATH, L'\n');
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			if (length == 0)
			{
				return {};
			}
			if (length < buffer.size())
			{
				buffer.resize(length);
				break;
			}
			// 収まらなかったときは length == buffer.size() で返ってくる。広げないと同じ結果のまま回り続ける
			buffer.resize(buffer.size() * 2);
		}
			return std::filesystem::path(buffer).parent_path();
	}
#endif

	inline std::filesystem::path FindDirectoryUpward(const std::filesystem::path& directoryName,
		const std::filesystem::path& marker)
	{
		// アクセスできないフォルダがあっても
		// 例外止まらないようにする
		std::error_code error;

		for (std::filesystem::path dir = ExecutableDirectory();
			!dir.empty();
			dir = dir.parent_path())
		{
			const std::filesystem::path candidate = dir / directoryName;
			if (std::filesystem::exists(candidate / marker, error))
			{
				return candidate;
			}
			// ドライブのルート(C:/)は親が自分自身だから
			// 止めないと無限ループ
			if (dir == dir.parent_path())
			{
				break;
			}
		}
		return {};
	}
} // namespace TE
