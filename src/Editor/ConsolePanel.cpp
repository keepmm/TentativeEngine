/*****************************************************************//**
 * \file   ConsolePanel.cpp
 * \brief  ログを表示するパネル
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#include "Editor/ConsolePanel.hpp"

#include <imgui.h>

#include <cstdio>

namespace
{
	constexpr const char* kLevelNames[] = { "Info", "Warning", "Error" };
	const ImVec4 kLevelColors[] = {
		ImVec4(0.85f, 0.85f, 0.85f, 1.0f),
		ImVec4(1.00f, 0.80f, 0.30f, 1.0f),
		ImVec4(1.00f, 0.40f, 0.40f, 1.0f),
	};
}

ConsolePanel::ConsolePanel()
{
	m_Entries.reserve(kMaxEntries);
	SetLogSink([this](LogLevel level, const std::string& message) { Add(level, message); });
}

ConsolePanel::~ConsolePanel()
{
	// 消えた後に Log から呼ばれないよう外す
	SetLogSink(nullptr);
}

void ConsolePanel::Add(LogLevel level, const std::string& text)
{
	std::lock_guard lock(m_Mutex);
	if (m_Entries.size() >= kMaxEntries)
	{
		for (std::size_t i = 0; i < kDropCount; ++i)
		{
			--m_Counts[static_cast<std::size_t>(m_Entries[i].level)];
		}
		m_Entries.erase(m_Entries.begin(), m_Entries.begin() + kDropCount);
	}
	m_Entries.push_back({ level, text });
	++m_Counts[static_cast<std::size_t>(level)];
}

void ConsolePanel::Clear()
{
	std::lock_guard lock(m_Mutex);
	m_Entries.clear();
	m_Counts[0] = m_Counts[1] = m_Counts[2] = 0;
}

void ConsolePanel::Draw(bool* open)
{
	if (!ImGui::Begin(kWindowName, open))
	{
		ImGui::End();
		return;
	}

	// ---- ツールバー ---- //
	if (ImGui::Button("Clear"))
	{
		Clear();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

	std::lock_guard lock(m_Mutex);
	// 重要度ごとの表示切り替え。Unity と同じく件数をボタンに出す
	for (std::size_t i = 0; i < 3; ++i)
	{
		ImGui::SameLine();
		char label[48];
		std::snprintf(label, sizeof(label), "%s %zu", kLevelNames[i], m_Counts[i]);
		ImGui::PushStyleColor(ImGuiCol_Text, kLevelColors[i]);
		ImGui::Checkbox(label, &m_ShowLevel[i]);
		ImGui::PopStyleColor();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##Filter", "Filter", m_Filter, sizeof(m_Filter));
	ImGui::Separator();

	// ---- 本文 ---- //
	if (ImGui::BeginChild("##ConsoleLog", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
	{
		const bool filtering = m_Filter[0] != '\0';
		for (const Entry& entry : m_Entries)
		{
			if (!m_ShowLevel[static_cast<std::size_t>(entry.level)])
			{
				continue;
			}
			if (filtering && entry.text.find(m_Filter) == std::string::npos)
			{
				continue;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, kLevelColors[static_cast<std::size_t>(entry.level)]);
			ImGui::TextUnformatted(entry.text.c_str(), entry.text.c_str() + entry.text.size());
			ImGui::PopStyleColor();
		}
		// 一番下を見ているときだけ追いかける（上を読んでいる最中に引き戻さない）
		if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		{
			ImGui::SetScrollHereY(1.0f);
		}
	}
	ImGui::EndChild();
	ImGui::End();
}
