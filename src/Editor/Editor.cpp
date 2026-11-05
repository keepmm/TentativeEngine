/*****************************************************************//**
 * \file   Editor.cpp
 * \brief  エディターの画面（DockSpace・メニューバー・各パネル）
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#include "Editor/Editor.hpp"

#include "ECS/World.hpp"
#include "Editor/ConsolePanel.hpp"
#include "Math/Intersection.hpp"
#include "Math/Ray.hpp"
#include "Reflection.hpp"
#include "Renderer/MeshRenderer.hpp"
#include "Renderer/RenderSystem.hpp"
#include "Renderer/Renderer.hpp"
#include "Scene/Camera.hpp"
#include "Scene/Name.hpp"
#include "Scene/Transform.hpp"
#include "UI/ImGuiRenderer.hpp"

#include <imgui.h>
#include <imgui_internal.h>	// DockBuilder（初期レイアウトを組むのに使う）
#include <ImGuizmo.h>

#include <Windows.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iterator>


using namespace TE;

namespace
{
	constexpr const char* kDockSpaceName = "EditorDockSpace";
	constexpr const char* kHierarchyName = "Hierarchy";
	constexpr const char* kInspectorName = "Inspector";
	constexpr const char* kContentBrowserName = "Content Browser";
	constexpr const char* kSceneViewname = "Scene";
	constexpr const char* kGameViewName = "Game";

	// 押して離すまでにこれ以上動いたら、クリックではなくドラッグ（カメラの回転）とみなす
	constexpr float kClickDragThreshold = 4.0f;

	// ---- Game View の機種 ---- //
	enum class DeviceKind : std::uint8_t
	{
		Free,		// パネルの大きさがそのままゲームの画面（スタンドアロンでウィンドウをリサイズしたのと同じ）
		Desktop,	// デスクトップの解像度（横長が標準）
		Mobile,		// スマホ・タブレット（縦長が標準。セーフエリアあり）
	};

	struct DeviceProfile
	{
		const char* name;
		DeviceKind kind;
		int width;			// 標準の向きでのピクセル数。Free は 0
		int height;
		int safeTop;		// Mobile のみ。標準の向きでの上下の帯（ピクセル。概算）
		int safeBottom;
	};

	// 0 番は Free にしておく（Editor::GameViewSettings の既定値と合わせる）
	constexpr DeviceProfile kDeviceProfiles[] =
	{
		{ "Free (Fit to panel)",       DeviceKind::Free,    0,    0,    0,   0 },
		{ "Desktop 1920x1080 (16:9)",  DeviceKind::Desktop, 1920, 1080, 0,   0 },
		{ "Desktop 1280x720 (16:9)",   DeviceKind::Desktop, 1280, 720,  0,   0 },
		{ "Desktop 2560x1080 (21:9)",  DeviceKind::Desktop, 2560, 1080, 0,   0 },
		{ "Android 20:9 (1080x2400)",  DeviceKind::Mobile,  1080, 2400, 84,  48 },
		{ "iPhone 15 (1179x2556)",     DeviceKind::Mobile,  1179, 2556, 177, 102 },
		{ "Tablet 4:3 (1536x2048)",    DeviceKind::Mobile,  1536, 2048, 0,   0 },
	};

	// ワールドの点を Scene の画像の上のピクセル位置へ。カメラの後ろ（w が 0 以下）なら false
	bool WorldToScreen(const Matrix4x4& viewProjection, const Vector3& point,
		const ImVec2& origin, const ImVec2& size, ImVec2& out)
	{
		// 行ベクトルなので (x, y, z, 1) を左から掛ける
		const float x = point.x * viewProjection.m[0][0] + point.y * viewProjection.m[1][0] + point.z * viewProjection.m[2][0] + viewProjection.m[3][0];
		const float y = point.x * viewProjection.m[0][1] + point.y * viewProjection.m[1][1] + point.z * viewProjection.m[2][1] + viewProjection.m[3][1];
		const float w = point.x * viewProjection.m[0][3] + point.y * viewProjection.m[1][3] + point.z * viewProjection.m[2][3] + viewProjection.m[3][3];
		if (w <= 1e-5f)
		{
			return false;
		}
		// NDC（-1〜1、y は上向き）→ 画像の左上が原点、y は下向き
		out.x = origin.x + (x / w * 0.5f + 0.5f) * size.x;
		out.y = origin.y + (0.5f - y / w * 0.5f) * size.y;
		return true;
	}

	ImGuizmo::OPERATION ToImGuizmo(int operation)
	{
		switch (operation)
		{
		case 1:  return ImGuizmo::ROTATE;
		case 2:  return ImGuizmo::SCALE;
		default: return ImGuizmo::TRANSLATE;
		}
	}
}

Editor::Editor(Renderer& renderer, ImGuiRenderer& imguiRenderer, World& world,
	const TypeRegistry& types, ConsolePanel& console)
	: m_Renderer(renderer), m_ImGuiRenderer(imguiRenderer), m_World(world), m_Types(types), m_Console(console)
{
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
}

void Editor::Draw(float deltaSeconds)
{
	// ImGuizmo は ImGui::NewFrame の後、毎フレーム 1 回
	ImGuizmo::BeginFrame();

	// メニューバーを先に作る。DockSpace はメニューバーを除いた残りの領域に敷かれる
	DrawMenuBar();

	const ImGuiID dockspaceId = ImGui::GetID(kDockSpaceName);
	if (m_ResetLayout || ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
	{
		BuildDefaultLayout(dockspaceId);
		m_ResetLayout = false;
	}
	// 中央には Scene ウィンドウを置くので、穴あきにはしない
	ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

	DrawSceneView(deltaSeconds);
	if (m_ShowGame)
	{
		DrawGameView();
	}
	else
	{
		// 閉じているときは描かない（RT も作らない）
		m_Renderer.SetViewEnabled(ViewId::Game, false);
	}
	if (m_ShowHierarchy) DrawHierarchy();
	if (m_ShowInspector) DrawInspector();
	if (m_ShowContentBrowser) DrawContentBrowser();
	if (m_ShowConsole) m_Console.Draw(&m_ShowConsole);
	if (m_ShowDemoWindow) ImGui::ShowDemoWindow(&m_ShowDemoWindow);
}

// ---------------------------------------------------//
//                  メニュー / レイアウト                //
// ---------------------------------------------------//

void Editor::DrawMenuBar()
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}
	if (ImGui::BeginMenu("File"))
	{
		// 閉じるとウィンドウの WM_CLOSE と同じ流れで終わる（Engine の Run が WM_QUIT で抜ける）
		if (ImGui::MenuItem("Exit", "Alt+F4"))
		{
			PostQuitMessage(0);
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View"))
	{
		bool gridVisible = m_Renderer.IsGridVisible();
		if (ImGui::MenuItem("Grid", "G", &gridVisible))
		{
			m_Renderer.SetGridVisible(gridVisible);
		}
		ImGui::MenuItem("Stats", nullptr, &m_ShowStats);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		ImGui::MenuItem(kGameViewName, nullptr, &m_ShowGame);
		ImGui::MenuItem(kHierarchyName, nullptr, &m_ShowHierarchy);
		ImGui::MenuItem(kInspectorName, nullptr, &m_ShowInspector);
		ImGui::MenuItem(ConsolePanel::kWindowName, nullptr, &m_ShowConsole);
		ImGui::MenuItem(kContentBrowserName, nullptr, &m_ShowContentBrowser);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout"))
		{
			// 閉じていたパネルも出し直してから、次のフレームで並べ直す
			m_ShowGame = m_ShowHierarchy = m_ShowInspector = m_ShowConsole = m_ShowContentBrowser = true;
			m_ResetLayout = true;
		}
		ImGui::Separator();
		ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

void Editor::BuildDefaultLayout(unsigned int dockspaceId)
{
	// Unity の既定に近い並び: 左 Hierarchy / 右 Inspector / 下 Console と Content Browser（タブ）/ 中央 3D
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(dockspaceId);
	// DockSpace は imgui_internal.h 側の列挙なので、型を揃えてから合わせる
	ImGui::DockBuilderAddNode(dockspaceId, static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace));
	// 分ける前に大きさを決めておく（決めないと分けたときの比率が崩れる。DockBuilder の決まり）
	ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

	ImGuiID center = dockspaceId;
	const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
	const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
	const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);

	ImGui::DockBuilderDockWindow(kSceneViewname, center);
	ImGui::DockBuilderDockWindow(kGameViewName, center);
	ImGui::DockBuilderDockWindow(kHierarchyName, left);
	ImGui::DockBuilderDockWindow(kInspectorName, right);
	ImGui::DockBuilderDockWindow(ConsolePanel::kWindowName, bottom);
	ImGui::DockBuilderDockWindow(kContentBrowserName, bottom);
	ImGui::DockBuilderFinish(dockspaceId);
}

void Editor::DrawSceneView(float deltaSeconds)
{
	// 画面をパネルいっぱいに出すので余白0にする・スクロールさせない
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	// NoMove: 画像の上のドラッグでウィンドウが動かないように（カメラとギズモに使う）。MenuBar: ギズモの切り替え
	const bool visible = ImGui::Begin(kSceneViewname, nullptr,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar);

	ImGui::PopStyleVar();
	if (!visible)
	{
		// タブの裏に隠れているときは3D操作させない（描きもしない）
		m_Renderer.SetViewEnabled(ViewId::Scene, false);
		m_SceneViewActive = false;
		m_SceneViewDragging = false;
		m_PickPressed = false;
		ImGui::End();
		return;
	}
	DrawSceneMenuBar();

	// パネルの大きさが変わったらRTを作りなおす
	const ImVec2 size = ImGui::GetContentRegionAvail();
	const auto width = static_cast<std::uint32_t>(std::max(size.x, 1.0f));
	const auto height = static_cast<std::uint32_t>(std::max(size.y, 1.0f));
	m_Renderer.ResizeView(ViewId::Scene, width, height);
	m_Renderer.SetViewEnabled(ViewId::Scene, true);
	if (m_Renderer.ViewColor(ViewId::Scene).get() != m_SceneTextureSource)
	{
		if (m_SceneTextureID != 0)
		{
			m_ImGuiRenderer.RemoveTexture(m_SceneTextureID);
		}
		m_SceneTextureID = m_ImGuiRenderer.AddTexture(m_Renderer.ViewColor(ViewId::Scene));
		m_SceneTextureSource = m_Renderer.ViewColor(ViewId::Scene).get();
	}

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::Image(ImTextureRef(static_cast<ImTextureID>(m_SceneTextureID)), size);

	const bool hovered = ImGui::IsItemHovered();

	// ---- ギズモと選択の枠 ---- //
	CameraMatrices camera;
	bool gizmoHot = false;
	const bool hasCamera = ComputeSceneCamera(camera);
	if (hasCamera)
	{
		m_Renderer.SetViewCamera(ViewId::Scene, camera.view, camera.projection, camera.clear);
	}
	const Matrix4x4 viewProjection = camera.view * camera.projection;	// 行ベクトルなので「ビュー → 投影」の順
	if (hasCamera)
	{
		DrawSelectionBounds(origin, size, viewProjection);
		gizmoHot = DrawGizmo(origin, size, camera.view, camera.projection);
	}

	// ---- W / E / R でギズモを切り替える（Unity と同じ）---- //
	// 文字を打っている間と、右ドラッグでカメラを回している間は切り替えない
	if ((hovered || ImGui::IsWindowFocused()) && !ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoOperation = GizmoOperation::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoOperation = GizmoOperation::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoOperation = GizmoOperation::Scale;
	}

	// ---- カメラに渡すか ---- //
	// 画面の上（ギズモ以外）でボタンを押したら、離すまで 3D 側が持つ。
	// 何も押していないときは、画像の上にあればホイールを渡す。ギズモの上ではカメラを動かさない
	const bool anyDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
		ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
		ImGui::IsMouseDown(ImGuiMouseButton_Middle);
	if (hovered && !gizmoHot && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
	{
		m_SceneViewDragging = true;
	}
	if (!anyDown)
	{
		m_SceneViewDragging = false;
	}
	m_SceneViewActive = (m_SceneViewDragging || (hovered && !anyDown)) && !gizmoHot;

	// ---- クリックで選ぶ ---- //
	// 左ボタンは回転にも使うので、押してから離すまでほとんど動かなかったときだけ「クリック」とみなす
	if (hovered && !gizmoHot && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_PickPressed = true;
	}
	if (m_PickPressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		const float dragged = ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left];
		if (hovered && hasCamera && dragged < kClickDragThreshold * kClickDragThreshold)
		{
			PickAt(ImGui::GetMousePos(), origin, size, viewProjection);
		}
		m_PickPressed = false;
	}

	if (m_ShowStats)
	{
		DrawStatsOverlay(origin, deltaSeconds);
	}
	ImGui::End();
}

// ---------------------------------------------------//
//                     Game View                       //
// ---------------------------------------------------//

void Editor::DrawGameView()
{
	// スクロールバーは Fit と Free のときだけ消す（等倍で大きいときは、スクロールで見られるようにする）
	const bool hideScrollbar = m_Game.fit || kDeviceProfiles[m_Game.profile].kind == DeviceKind::Free;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar;
	if (hideScrollbar)
	{
		flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	}
	const bool visible = ImGui::Begin(kGameViewName, &m_ShowGame, flags);
	ImGui::PopStyleVar();
	if (!visible)
	{
		// タブの裏に隠れているときは描かない
		m_Renderer.SetViewEnabled(ViewId::Game, false);
		ImGui::End();
		return;
	}
	DrawGameMenuBar();

	// ツールバーで機種が変わったかもしれないので、ここで引き直す
	const DeviceProfile& profile = kDeviceProfiles[m_Game.profile];
	const ImVec2 avail = ImGui::GetContentRegionAvail();

	// ゲームから見える画面の大きさ。
	// Free: パネルの大きさそのまま / それ以外: 機種の解像度を固定で使い、パネルには拡大縮小して収める
	int renderWidth = 0;
	int renderHeight = 0;
	if (profile.kind == DeviceKind::Free)
	{
		renderWidth = std::max(1, static_cast<int>(avail.x));
		renderHeight = std::max(1, static_cast<int>(avail.y));
	}
	else
	{
		renderWidth = m_Game.rotated ? profile.height : profile.width;
		renderHeight = m_Game.rotated ? profile.width : profile.height;
	}
	m_Renderer.ResizeView(ViewId::Game, static_cast<std::uint32_t>(renderWidth), static_cast<std::uint32_t>(renderHeight));

	// Main Camera の目で描く。カメラが無ければ描かない（作ったばかりの RT は中身が未定義なので、表示もしない）
	CameraMatrices camera;
	const bool hasCamera = FindMainCamera(m_World, static_cast<float>(renderWidth) / static_cast<float>(renderHeight), camera);
	m_Renderer.SetViewEnabled(ViewId::Game, hasCamera);
	if (!hasCamera)
	{
		ImGui::TextDisabled("No Camera");
		ImGui::End();
		return;
	}
	m_Renderer.SetViewCamera(ViewId::Game, camera.view, camera.projection, camera.clear);

	// RT が作り直されたら ImGui 側のテクスチャも取り直す
	const RHITextureRef& gameColor = m_Renderer.ViewColor(ViewId::Game);
	if (gameColor.get() != m_GameTextureSource)
	{
		if (m_GameTextureID != 0)
		{
			m_ImGuiRenderer.RemoveTexture(m_GameTextureID);
		}
		m_GameTextureID = m_ImGuiRenderer.AddTexture(gameColor);
		m_GameTextureSource = gameColor.get();
	}

	// ---- アスペクト比を保って、中央に置く ---- //
	float scale = 1.0f;
	if (profile.kind != DeviceKind::Free)
	{
		scale = m_Game.fit
			? std::min(avail.x / static_cast<float>(renderWidth), avail.y / static_cast<float>(renderHeight))
			: m_Game.zoom;
	}
	const ImVec2 imageSize(static_cast<float>(renderWidth) * scale, static_cast<float>(renderHeight) * scale);
	const ImVec2 cursor = ImGui::GetCursorPos();
	ImGui::SetCursorPos(ImVec2(cursor.x + std::max(0.0f, (avail.x - imageSize.x) * 0.5f),
		cursor.y + std::max(0.0f, (avail.y - imageSize.y) * 0.5f)));
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::Image(ImTextureRef(static_cast<ImTextureID>(m_GameTextureID)), imageSize);

	// ---- セーフエリア（ノッチ・ホームバー）の帯。スマホの機種だけ。横持ちは上下を左右に回す ---- //
	if (profile.kind == DeviceKind::Mobile && m_Game.showSafeArea)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 color = IM_COL32(255, 60, 60, 70);
		const float first = static_cast<float>(profile.safeTop) * scale;
		const float last = static_cast<float>(profile.safeBottom) * scale;
		const ImVec2 end(origin.x + imageSize.x, origin.y + imageSize.y);
		if (!m_Game.rotated)
		{
			drawList->AddRectFilled(origin, ImVec2(end.x, origin.y + first), color);
			drawList->AddRectFilled(ImVec2(origin.x, end.y - last), end, color);
		}
		else
		{
			drawList->AddRectFilled(origin, ImVec2(origin.x + first, end.y), color);
			drawList->AddRectFilled(ImVec2(end.x - last, origin.y), end, color);
		}
	}
	ImGui::End();
}

void Editor::DrawGameMenuBar()
{
	if (!ImGui::BeginMenuBar())
	{
		return;
	}
	ImGui::SetNextItemWidth(230.0f);
	if (ImGui::BeginCombo("##Device", kDeviceProfiles[m_Game.profile].name))
	{
		for (int i = 0; i < static_cast<int>(std::size(kDeviceProfiles)); ++i)
		{
			if (ImGui::Selectable(kDeviceProfiles[i].name, i == m_Game.profile))
			{
				m_Game.profile = i;
			}
		}
		ImGui::EndCombo();
	}
	const DeviceKind kind = kDeviceProfiles[m_Game.profile].kind;

	// Free はパネルの大きさがそのまま画面になるので、回転・Fit・倍率は使わない
	ImGui::BeginDisabled(kind == DeviceKind::Free);
	ImGui::Checkbox("Rotate", &m_Game.rotated);
	ImGui::Checkbox("Fit", &m_Game.fit);
	ImGui::BeginDisabled(m_Game.fit);
	ImGui::SetNextItemWidth(80.0f);
	ImGui::SliderFloat("##Zoom", &m_Game.zoom, 0.1f, 1.0f, "%.2fx");
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	// セーフエリアはスマホの機種だけ
	ImGui::BeginDisabled(kind != DeviceKind::Mobile);
	ImGui::Checkbox("Safe Area", &m_Game.showSafeArea);
	ImGui::EndDisabled();
	ImGui::EndMenuBar();
}

void Editor::DrawStatsOverlay(const ImVec2& origin, float deltaSeconds)
{
	// Scene の画像の左上に、半透明の背景つきで重ねる（別ウィンドウにしないので、ドッキングの邪魔をしない）
	char text[96];
	std::snprintf(text, sizeof(text), "FPS %.1f (%.2f ms)\nEntities %zu",
		ImGui::GetIO().Framerate, deltaSeconds * 1000.0f, m_World.AliveCount());
	constexpr float kMargin = 8.0f;
	constexpr float kPadding = 6.0f;
	const ImVec2 textSize = ImGui::CalcTextSize(text);
	const ImVec2 min(origin.x + kMargin, origin.y + kMargin);
	const ImVec2 max(min.x + textSize.x + kPadding * 2.0f, min.y + textSize.y + kPadding * 2.0f);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(min, max, IM_COL32(0, 0, 0, 110), 4.0f);
	drawList->AddText(ImVec2(min.x + kPadding, min.y + kPadding), IM_COL32_WHITE, text);
}

// ---------------------------------------------------//
//                 Scene View の上の操作                //
// ---------------------------------------------------//

bool Editor::ComputeSceneCamera(CameraMatrices& out)
{
	// Scene の RT の縦横比で作る。ここで作った行列を Renderer に渡すので、3D とギズモ・枠の位置が揃う
	return FindMainCamera(m_World, m_Renderer.ViewAspect(ViewId::Scene), out);
}

void Editor::DrawSceneMenuBar()
{
	if (!ImGui::BeginMenuBar())
	{
		return;
	}
	// 今の種類がわかるよう、選んでいるものにチェックを付ける
	if (ImGui::MenuItem("Move", "W", m_GizmoOperation == GizmoOperation::Translate)) m_GizmoOperation = GizmoOperation::Translate;
	if (ImGui::MenuItem("Rotate", "E", m_GizmoOperation == GizmoOperation::Rotate)) m_GizmoOperation = GizmoOperation::Rotate;
	if (ImGui::MenuItem("Scale", "R", m_GizmoOperation == GizmoOperation::Scale)) m_GizmoOperation = GizmoOperation::Scale;
	ImGui::Separator();
	// 拡縮はいつも物体の軸で行う（ImGuizmo の決まり）ので、表示もそれに合わせる
	if (ImGui::MenuItem(m_GizmoLocal ? "Local" : "World"))
	{
		m_GizmoLocal = !m_GizmoLocal;
	}
	ImGui::EndMenuBar();
}

bool Editor::DrawGizmo(const ImVec2& origin, const ImVec2& size, const Matrix4x4& view, const Matrix4x4& projection)
{
	Transform* transform = m_World.TryGetComponent<Transform>(m_Selected);
	if (transform == nullptr)
	{
		return false;
	}

	// Scene の画像の上に、このウィンドウの描画リストで重ねる
	ImGuizmo::SetOrthographic(false);
	// 既定ではカメラに向くよう軸の向きを反転して描く。Unity と同じく、+X / +Y / +Z をいつも同じ向きで見せる
	ImGuizmo::AllowAxisFlip(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
	// カメラを回している最中に軸の上を通っても、つかまないようにする
	ImGuizmo::Enable(!m_SceneViewDragging);

	// ImGuizmo の行列は「行ベクトル・平行移動が 4 行目」で、Matrix4x4 とメモリの並びが同じなので、そのまま渡せる
	Matrix4x4 world = Matrix4x4::TRS(transform->position, transform->rotation, transform->scale);
	const ImGuizmo::MODE mode = m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
	if (ImGuizmo::Manipulate(&view.m[0][0], &projection.m[0][0],
		ToImGuizmo(static_cast<int>(m_GizmoOperation)), mode, &world.m[0][0]))
	{
		// 行列から位置・回転・拡大率に戻す。行 0〜2 は「拡大率 × 回転後の X / Y / Z 軸」、行 3 が位置
		const Vector3 axisX{ world.m[0][0], world.m[0][1], world.m[0][2] };
		const Vector3 axisY{ world.m[1][0], world.m[1][1], world.m[1][2] };
		const Vector3 axisZ{ world.m[2][0], world.m[2][1], world.m[2][2] };
		transform->position = world.GetPosition();
		transform->scale = Vector3{ axisX.Magnitude(), axisY.Magnitude(), axisZ.Magnitude() };
		// 前（Z）と上（Y）から回転を作り直す。拡大率は LookRotation の中で正規化されて消える
		transform->rotation = Quaternion::LookRotation(axisZ, axisY);
	}
	return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

void Editor::DrawSelectionBounds(const ImVec2& origin, const ImVec2& size, const Matrix4x4& viewProjection)
{
	const LocalToWorld* localToWorld = m_World.TryGetComponent<LocalToWorld>(m_Selected);
	const MeshRenderer* meshRenderer = m_World.TryGetComponent<MeshRenderer>(m_Selected);
	if (localToWorld == nullptr || meshRenderer == nullptr)
	{
		return;
	}
	const Bounds* bounds = m_Renderer.MeshBounds(meshRenderer->mesh);
	if (bounds == nullptr)
	{
		return;
	}

	// 箱の 8 つの角を画面に写す。1 つでもカメラの後ろにあれば、線が裏返って変な所に出るので描かない
	const Vector3 lo = bounds->Min();
	const Vector3 hi = bounds->Max();
	ImVec2 corners[8];
	for (int i = 0; i < 8; ++i)
	{
		// i のビット 0 / 1 / 2 で x / y / z の min と max を選ぶ
		const Vector3 local{ (i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z };
		if (!WorldToScreen(viewProjection, localToWorld->value.MultiplyPoint3x4(local), origin, size, corners[i]))
		{
			return;
		}
	}
	// 12 本の辺。ビットが 1 つだけ違う角どうしをつなぐ
	constexpr int kEdges[12][2] = {
		{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },	// x 方向
		{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },	// y 方向
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },	// z 方向
	};
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 color = IM_COL32(255, 170, 40, 220);	// Unity の選択の橙
	for (const auto& edge : kEdges)
	{
		drawList->AddLine(corners[edge[0]], corners[edge[1]], color, 1.5f);
	}
}

void Editor::PickAt(const ImVec2& mouse, const ImVec2& origin, const ImVec2& size, const Matrix4x4& viewProjection)
{
	// マウスの位置を NDC に戻し、ニア面（深度 0）とファー面（深度 1）の点を結んでレイにする
	const float ndcX = (mouse.x - origin.x) / size.x * 2.0f - 1.0f;
	const float ndcY = 1.0f - (mouse.y - origin.y) / size.y * 2.0f;
	const Matrix4x4 inverse = viewProjection.Inverse();
	const Vector3 nearPoint = inverse.MultiplyPoint({ ndcX, ndcY, 0.0f });
	const Vector3 farPoint = inverse.MultiplyPoint({ ndcX, ndcY, 1.0f });
	const Ray ray(nearPoint, farPoint - nearPoint);

	// メッシュの箱は物体の空間なので、レイを物体の空間へ持っていって当てる（回転しても箱がずれない）。
	// 距離は物体ごとに縮尺が違うので、当たった点をワールドに戻してから比べる
	Entity nearest = kNullEntity;
	float nearestDistance = Mathf::Infinity;
	m_World.Each<LocalToWorld, MeshRenderer>([&](Entity entity, const LocalToWorld& localToWorld, const MeshRenderer& meshRenderer)
		{
			const Bounds* bounds = m_Renderer.MeshBounds(meshRenderer.mesh);
			if (bounds == nullptr)
			{
				return;
			}
			const Matrix4x4 worldToLocal = localToWorld.value.Inverse();
			const Ray localRay(worldToLocal.MultiplyPoint3x4(ray.origin), worldToLocal.MultiplyVector(ray.direction));
			float localDistance = 0.0f;
			if (!Intersection::RayAABB(localRay, bounds->Min(), bounds->Max(), localDistance))
			{
				return;
			}
			const Vector3 hit = localToWorld.value.MultiplyPoint3x4(localRay.GetPoint(localDistance));
			const float distance = (hit - ray.origin).Magnitude();
			if (distance < nearestDistance)
			{
				nearestDistance = distance;
				nearest = entity;
			}
		});
	// 何も無い所をクリックしたら選択を外す（Unity と同じ）
	Select(nearest);
}

// ---------------------------------------------------//
//                       パネル                         //
// ---------------------------------------------------//

void Editor::DrawHierarchy()
{
	if (!ImGui::Begin(kHierarchyName, &m_ShowHierarchy))
	{
		ImGui::End();
		return;
	}
	// 全 Entity を並べる。Name があればその名前、無ければ番号。
	// 並びはアーキタイプ → チャンクの順（作った順ではない）。親子は Transform の階層を入れてから
	Entity toDestroy = kNullEntity;
	m_World.EachEntity([&](Entity entity)
		{
			char label[96];
			const Name* name = m_World.TryGetComponent<Name>(entity);
			if (name != nullptr && !name->value.Empty())
			{
				std::snprintf(label, sizeof(label), "%s", name->value.CStr());
			}
			else
			{
				std::snprintf(label, sizeof(label), "Entity %u", entity.index);
			}
			ImGui::PushID(static_cast<int>(entity.index));
			if (ImGui::Selectable(label, entity == m_Selected))
			{
				Select(entity);
			}
			// 右クリックで消す。EachEntity の中では構造を変えられないので、回し終えてから
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Delete"))
				{
					toDestroy = entity;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		});

	// 何もない所をクリックしたら選択を外す（Unity と同じ）
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
	{
		Select(kNullEntity);
	}
	// 何もない所の右クリックで作る
	bool create = false;
	if (ImGui::BeginPopupContextWindow("##HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		create = ImGui::MenuItem("Create Empty");
		ImGui::EndPopup();
	}
	ImGui::End();

	if (!toDestroy.IsNull())
	{
		if (toDestroy == m_Selected)
		{
			Select(kNullEntity);
		}
		m_World.DestroyEntity(toDestroy);
	}
	if (create)
	{
		// Unity の Create Empty と同じく、名前と位置だけ持たせる
		Select(m_World.CreateEntity(Name{ "Entity" }, Transform{}, LocalToWorld{}));
	}
}

void Editor::Select(Entity entity)
{
	if (entity != m_Selected)
	{
		m_EulerCache.clear();
	}
	m_Selected = entity;
}

void Editor::DrawInspector()
{
	if (!ImGui::Begin(kInspectorName, &m_ShowInspector))
	{
		ImGui::End();
		return;
	}
	if (m_Selected.IsNull() || !m_World.IsAlive(m_Selected))
	{
		ImGui::TextDisabled("Nothing selected");
		ImGui::End();
		return;
	}
	ImGui::TextDisabled("Entity %u (gen %u)", m_Selected.index, m_Selected.generation);
	ImGui::Separator();

	// 付け外しは一覧を回し終えてから（回している型の並びが、引っ越しで別のアーキタイプのものに変わるため）
	constexpr ComponentTypeID kNone = ~ComponentTypeID{ 0 };
	ComponentTypeID toRemove = kNone;
	ComponentTypeID toAdd = kNone;

	for (const ComponentTypeID id : m_World.GetComponentTypes(m_Selected))
	{
		const TypeInfo* type = m_Types.Find(id);
		char header[64];
		if (type != nullptr)
		{
			std::snprintf(header, sizeof(header), "%s", type->name);
		}
		else
		{
			std::snprintf(header, sizeof(header), "(not reflected) #%u", id);
		}

		ImGui::PushID(static_cast<int>(id));
		const bool open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
		// 見出しの右クリックで外す
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Remove Component"))
			{
				toRemove = id;
			}
			ImGui::EndPopup();
		}
		if (open && type != nullptr)
		{
			auto* component = static_cast<std::byte*>(m_World.TryGetComponent(m_Selected, id));
			for (const FieldInfo& field : type->fields)
			{
				DrawField(field, component + field.offset, id);
			}
		}
		ImGui::PopID();
	}

	// ---- Add Component ---- //
	ImGui::Separator();
	const float buttonWidth = 200.0f;
	ImGui::SetCursorPosX(std::max((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f, 0.0f));
	if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0.0f)))
	{
		ImGui::OpenPopup("##AddComponent");
	}
	if (ImGui::BeginPopup("##AddComponent"))
	{
		// 登録済みで、まだ持っていなくて、既定値で作れる型だけ出す
		for (const ComponentTypeID id : m_Types.RegisteredTypes())
		{
			if (m_World.HasComponent(m_Selected, id) || GetComponentInfo(id).defaultConstruct == nullptr)
			{
				continue;
			}
			if (ImGui::MenuItem(m_Types.Find(id)->name))
			{
				toAdd = id;
			}
		}
		ImGui::EndPopup();
	}
	ImGui::End();

	if (toRemove != kNone)
	{
		m_World.RemoveComponent(m_Selected, toRemove);
	}
	if (toAdd != kNone)
	{
		m_World.AddComponent(m_Selected, toAdd);
	}
}

void Editor::DrawField(const FieldInfo& field, void* data, ComponentTypeID type)
{
	// 計算結果や番号など、書き換えさせないものは灰色で出すだけ
	const bool readOnly = HasFlag(field.flags, FieldFlags::ReadOnly);
	ImGui::BeginDisabled(readOnly);
	ImGui::PushID(field.name);

	switch (field.type)
	{
	case FieldType::Bool:
		ImGui::Checkbox(field.name, static_cast<bool*>(data));
		break;
	case FieldType::Int32:
		ImGui::DragInt(field.name, static_cast<int*>(data));
		break;
	case FieldType::UInt32:
		ImGui::DragScalar(field.name, ImGuiDataType_U32, data);
		break;
	case FieldType::Float:
		ImGui::DragFloat(field.name, static_cast<float*>(data), 0.01f);
		break;
	case FieldType::Vector2:
		ImGui::DragFloat2(field.name, static_cast<float*>(data), 0.01f);
		break;
	case FieldType::Vector3:
		ImGui::DragFloat3(field.name, static_cast<float*>(data), 0.01f);
		break;
	case FieldType::Vector4:
		ImGui::DragFloat4(field.name, static_cast<float*>(data), 0.01f);
		break;
	case FieldType::Color:
		ImGui::ColorEdit4(field.name, static_cast<float*>(data), ImGuiColorEditFlags_Float);
		break;
	case FieldType::String:
		ImGui::InputText(field.name, static_cast<char*>(data), field.size);
		break;
	case FieldType::Quaternion:
	{
		auto* rotation = static_cast<Quaternion*>(data);
		auto it = std::find_if(m_EulerCache.begin(), m_EulerCache.end(), [&](const EulerCache& cache)
			{
				return cache.type == type && cache.offset == field.offset;
			});
		if (it == m_EulerCache.end())
		{
			m_EulerCache.push_back({ type, field.offset, *rotation, rotation->EulerAngles() });
			it = m_EulerCache.end() - 1;
		}
		else if (std::memcmp(&it->source, rotation, sizeof(Quaternion)) != 0)
		{
			// 自分が書いた後に誰かが変えた（Spin が回したなど）。今の値から取り直す
			it->source = *rotation;
			it->euler = rotation->EulerAngles();
		}
		if (ImGui::DragFloat3(field.name, &it->euler.x, 0.5f))
		{
			*rotation = Quaternion::Euler(it->euler);
			it->source = *rotation;
		}
		break;
	}
	}

	ImGui::PopID();
	ImGui::EndDisabled();
}

void Editor::DrawContentBrowser()
{
	if (!ImGui::Begin(kContentBrowserName, &m_ShowContentBrowser))
	{
		ImGui::End();
		return;
	}
	ImGui::TextDisabled("assets/ browser: not yet");
	ImGui::End();
}
