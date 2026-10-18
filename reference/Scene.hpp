#pragma once

#include "RenderContext.hpp"
#include "World.hpp"
#include "PhysicsWorld.hpp"

enum class SceneState : uint8_t
{
	Loading,
	Active,
	Paused,
	Unloading,
	Unloaded
};

class Scene
{
public:
	Scene();
	Scene(const Scene&) = delete;
	Scene& operator=(const Scene&) = delete;
	Scene(Scene&&) = delete;
	Scene& operator=(Scene&&) = delete;

	virtual ~Scene() {};

	/// @brief シーンの初期化
	virtual void OnLoad() {};

	/// @brief シーンの開始
	virtual void OnStart() {};

	/// @brief シーンの更新処理
	/// @param deltatime 経過時間
	virtual void Update(float deltatime) = 0;

	/// @brief シーンの更新処理（物理演算など、一定時間ごとに更新したい処理をここに記述）
	/// @param fixedDeltatime 
	virtual void FixedUpdate(float fixedDeltatime) {};

	/// @brief シーンの更新処理（Updateの後に呼び出される。描画前の最終調整などに使用）
	/// @param deltatime 
	virtual void LateUpdate(float deltatime) {};

	virtual void OnActivate() {};

	void virtual OnDeactivate() {};

	/// @brief シーンの一時停止
	virtual void OnPause() {};

	/// @brief シーンの再開
	virtual void OnResume() {};

	/// @brief シーンの描画
	/// @param renderContext 描画に必要な構造体 
	virtual void Draw(_In_ const RenderContext& renderContext) = 0;

	/// @brief シーンのアンロード
	virtual void OnUnload() {};

	SceneState GetState() const { return m_State; }
	void SetState(_In_ SceneState state) { m_State = state; }

	const std::string& GetSceneName() const { return m_SceneName; }
	void SetSceneName(_In_ const std::string& name) { m_SceneName = name; }

	World& GetWorld() { return m_World; }
	PhysicsWorld* GetPhysicsWorld() const { return m_PhysicsWorld.get(); }
	PhysicsWorld& EnsurePhysicsWorld();
	bool HasPhysicsWorld() const;

	/// @brief シーンがロード済みか
	/// @return ロード済みであればtrue、それ以外はfalse
	bool IsLoaded() const
	{
		return m_State != SceneState::Unloaded
			&& m_State != SceneState::Loading
			&& m_State != SceneState::Unloading;
	}

	/// @brief シーンがアクティブか
	/// @return アクティブであればtrue、それ以外はfalse
	bool IsActive() const { return m_State == SceneState::Active; }

	/// @brief シーンを初期化(World/PhysicsWorldをクリア)
	void ResetWorld();

	/// @brief PhysicsWorldの破棄
	void ResetPhysicsWorld();

	const std::string& GetSkyboxPath() const { return m_SkyboxPath; }
	void SetSkyboxPath(_In_ const std::string& path) { m_SkyboxPath = path; }
protected:
	World m_World;
	std::unique_ptr<PhysicsWorld> m_PhysicsWorld;
	SceneState m_State = SceneState::Unloaded;
	std::string m_SceneName;
	std::string m_SkyboxPath/* = "Assets/Texture/sky.hdr"*/;
};

