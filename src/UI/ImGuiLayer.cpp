/*****************************************************************//**
 * \file   ImGuiLayer.cpp
 * \brief  ImGuiのコンテキストとWin32の入力をつなぐ
 * 
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#include "UI/ImGuiLayer.hpp"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImGuiLayer::ImGuiLayer(HWND hWnd)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();

	// 既定のフォントには日本語が無いので、Windows に入っている日本語フォントを使う。
	// 1.92 からは使った文字だけをその場でアトラスに足すので、文字の範囲（GlyphRanges）は指定しなくてよい
	constexpr float kFontSize = 16.0f;
	const char* const kJapaneseFonts[] = {
		"C:\\Windows\\Fonts\\meiryo.ttc",
		"C:\\Windows\\Fonts\\YuGothM.ttc",
		"C:\\Windows\\Fonts\\msgothic.ttc",
	};
	for (const char* path : kJapaneseFonts)
	{
		if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES && io.Fonts->AddFontFromFileTTF(path, kFontSize) != nullptr)
		{
			break;
		}
	}
	// どれも無ければ既定のフォント（英数字だけ）のまま

	if (!ImGui_ImplWin32_Init(hWnd))
	{
		ImGui::DestroyContext();
		throw std::runtime_error("ImGuiのWin32バックエンドの初期化に失敗しました");
	}
}

ImGuiLayer::~ImGuiLayer()
{
	// 放棄
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

bool ImGuiLayer::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
}

void ImGuiLayer::BeginFrame()
{
	// 表示サイズ・時間・マウスの位置などをWin32から読む
	ImGui_ImplWin32_NewFrame();

	// そのあとにImGui::NewFrame
	ImGui::NewFrame();
}

ImDrawData* ImGuiLayer::EndFrame()
{
	ImGui::Render();
	return ImGui::GetDrawData();
}

bool ImGuiLayer::WantCaptureMouse() const
{
	return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantCaptureKeyboard() const
{
	return ImGui::GetIO().WantCaptureKeyboard;
}
