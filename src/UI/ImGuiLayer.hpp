/*****************************************************************//**
 * \file   ImGuiLayer.hpp
 * \brief  ImGuiのコンテキストとWin32の入力をつなぐ
 * 
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#pragma once

#include <Windows.h>

struct ImDrawData;

/// @brief コンテキストの持ち主(描画は持たない)
/// BeginFrame -> ImGui::Begin / End でUIを構築 -> EndFrame
class ImGuiLayer
{
public:
	explicit ImGuiLayer(HWND hWnd);
	~ImGuiLayer();

	ImGuiLayer(const ImGuiLayer&) = delete;
	ImGuiLayer& operator=(const ImGuiLayer&) = delete;

	// ImGuiが使ったメッセージなら true
	bool HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	void BeginFrame();

	ImDrawData* EndFrame();

	// UIがマウス / キーボードを使っている間はゲーム側に渡さない

	bool WantCaptureMouse() const;
	bool WantCaptureKeyboard() const;
};
