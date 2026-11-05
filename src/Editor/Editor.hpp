/*****************************************************************//**
 * \file   Editor.hpp
 * \brief  エディターの画面（DockSpace・メニューバー・各パネル）
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#pragma once

#include "ECS/ComponentType.hpp"
#include "ECS/Entity.hpp"
#include "Math/Matrix4x4.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Vector3.hpp"

#include <cstdint>
#include <vector>

class ConsolePanel;
class ImGuiRenderer;
class Renderer;
class IRHITexture;
struct ImVec2;
namespace TE
{
	class World;
	class TypeRegistry;
	struct FieldInfo;
	struct CameraMatrices;
}

/// @brief ImGui のフレームの中で Draw を 1 回呼ぶ。描画（GPU）は持たない
/// @note  中央には Scene と Game のウィンドウを置き（タブ）、Renderer が 3D を描いた RT を ImGui::Image で表示する
class Editor
{
public:
	Editor(Renderer& renderer, ImGuiRenderer& imguiRenderer, TE::World& world,
		const TE::TypeRegistry& types, ConsolePanel& console);

	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;

	void Draw(float deltaSeconds);

	/// @brief 3D 側（カメラ）にマウスを渡してよいか。前のフレームの Draw で決まる
	bool IsSceneViewActive() const { return m_SceneViewActive; }

private:
	void DrawMenuBar();
	void BuildDefaultLayout(unsigned int dockspaceId);
	void DrawSceneView(float deltaSeconds);
	// ---- Game View ---- //
	// Main Camera（Camera を持つ最初の Entity）の目で、機種の解像度の RT に描いたものを出す
	void DrawGameView();
	void DrawGameMenuBar();
	// ---- Scene View の上の操作 ---- //
	// シーンのカメラ（Camera を持つ最初の Entity）の行列。Game View と同じ FindMainCamera で作る。カメラが無ければ false
	bool ComputeSceneCamera(TE::CameraMatrices& out);
	void DrawSceneMenuBar();
	// 選んでいる Entity の Transform をギズモで動かす。ギズモの上にマウスがあるか、つかんでいれば true
	bool DrawGizmo(const ImVec2& origin, const ImVec2& size, const TE::Matrix4x4& view, const TE::Matrix4x4& projection);
	// 選んでいる Entity のメッシュの箱を線で描く
	void DrawSelectionBounds(const ImVec2& origin, const ImVec2& size, const TE::Matrix4x4& viewProjection);
	// 画面上の点を通るレイで、一番手前のメッシュを選ぶ（箱で判定）。何も無ければ選択を外す
	void PickAt(const ImVec2& mouse, const ImVec2& origin, const ImVec2& size, const TE::Matrix4x4& viewProjection);
	void DrawStatsOverlay(const ImVec2& origin, float deltaSeconds);
	void DrawHierarchy();
	void DrawInspector();
	// フィールド 1 つぶんの編集 UI。data はフィールドの先頭（コンポーネントの先頭 + offset）
	void DrawField(const TE::FieldInfo& field, void* data, TE::ComponentTypeID type);
	void DrawContentBrowser();
	void Select(TE::Entity entity);

	Renderer& m_Renderer;
	ImGuiRenderer& m_ImGuiRenderer;
	TE::World& m_World;
	const TE::TypeRegistry& m_Types;
	ConsolePanel& m_Console;

	TE::Entity m_Selected = TE::kNullEntity;

	// クォータニオンを編集中のオイラー角。毎フレーム「クォータニオン → オイラー」と往復させると、
	// ジンバルロック付近で値が飛ぶので、自分で変えた値を覚えておく（Unity と同じ）。
	// 覚えたときのクォータニオンと今の値が違えば（System が回したなど）、今の値から取り直す
	struct EulerCache
	{
		TE::ComponentTypeID type;
		std::uint64_t offset;
		TE::Quaternion source;
		TE::Vector3 euler;
	};
	std::vector<EulerCache> m_EulerCache;	// 選んでいる Entity のぶんだけ。選び直したら空にする

	// SceneViewのRTをimguiに渡したID
	std::uint64_t m_SceneTextureID = 0;
	const IRHITexture* m_SceneTextureSource = nullptr;
	bool m_SceneViewActive = false;
	bool m_SceneViewDragging = false;
	// 左ボタンを Scene の上（ギズモ以外）で押した。離したときに動いていなければクリック選択にする
	bool m_PickPressed = false;

	// Game View の表示設定。機種の表（DeviceProfile）は Editor.cpp にある
	struct GameViewSettings
	{
		int profile = 0;			// kDeviceProfiles の添字。0 = Free（パネルの大きさがそのままゲームの画面）
		bool rotated = false;		// 機種の標準の向きから縦横を入れ替える
		bool fit = true;			// パネルに収める。false なら zoom の倍率で出す
		float zoom = 0.5f;			// fit = false のときの倍率
		bool showSafeArea = true;	// スマホの機種で、ノッチ・ホームバーの帯を重ねる
	};
	GameViewSettings m_Game;
	// Game View の RT を imgui に渡した ID
	std::uint64_t m_GameTextureID = 0;
	const IRHITexture* m_GameTextureSource = nullptr;

	// ギズモの種類と座標系。ImGuizmo の型をヘッダに出さないよう、自前の列挙で持つ
	enum class GizmoOperation : std::uint8_t { Translate, Rotate, Scale };
	GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
	bool m_GizmoLocal = false;	// false = ワールドの軸、true = 物体の軸

	bool m_ShowHierarchy = true;
	bool m_ShowInspector = true;
	bool m_ShowConsole = true;
	bool m_ShowContentBrowser = true;
	bool m_ShowGame = true;
	bool m_ShowStats = true;
	bool m_ShowDemoWindow = false;
	bool m_ResetLayout = false;
};
