/*****************************************************************//**
 * \file   ConsolePanel.hpp
 * \brief  ログを表示するパネル（Unity の Console に当たる）
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#pragma once

#include "Core/Log.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

/// @brief 作った時点から Log の出力を受け取って溜める。描画は Draw だけ（ImGui のフレームの中で呼ぶ）
/// @note  Log はどのスレッドからでも呼ばれうるので、溜める所は排他する
class ConsolePanel
{
public:
	ConsolePanel();
	~ConsolePanel();

	ConsolePanel(const ConsolePanel&) = delete;
	ConsolePanel& operator=(const ConsolePanel&) = delete;

	static constexpr const char* kWindowName = "Console";

	/// @param open ウィンドウの × で false になる
	void Draw(bool* open);

private:
	struct Entry
	{
		LogLevel level;
		std::string text;
	};

	void Add(LogLevel level, const std::string& text);
	void Clear();

	// 溜めすぎないよう、上限を超えたら古い方からまとめて捨てる（1 件ずつ消すと毎回詰め直しになる）
	static constexpr std::size_t kMaxEntries = 2000;
	static constexpr std::size_t kDropCount = 500;

	std::mutex m_Mutex;
	std::vector<Entry> m_Entries;
	std::size_t m_Counts[3] = {};	// 添字 = LogLevel。ボタンに件数を出す

	bool m_ShowLevel[3] = { true, true, true };
	bool m_AutoScroll = true;
	char m_Filter[128] = {};
};
