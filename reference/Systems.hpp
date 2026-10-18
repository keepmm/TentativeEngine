#pragma once

#include "GpuProfiler.hpp"

#include "World.hpp"
#include "Components.hpp"
#include "AnimatorClipCache.hpp"
#include "RenderContext.hpp"
#include "FramePipeline.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "FontAtlas.hpp"
#include "Input.hpp"
#include "MonoBehavior.hpp"
#include "Util.hpp"
#include "Debug.hpp"
#include "imguiinit.hpp"
#include "Animator.hpp"
#include <chrono>
#include "DirectX.hpp"
#include "d3dx12.h"
#include "Time.hpp"
#include "ModelLoader.hpp"
#include <sstream>
#include "AsyncLoader.hpp"
#include "Terrain.hpp"

class SpinSystem
{
public:
	void Update(World& world, float deltatime)
	{
		world.Each<TransformComponent, SpinComponent>(
			[deltatime](Entity, TransformComponent& transform, SpinComponent& spin)
			{
				spin.angle += spin.speed * deltatime;
				const auto q = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, spin.angle, 0.0f);
				DirectX::XMStoreFloat4(&transform.rotation, DirectX::XMQuaternionNormalize(q));
			}
		);
	}
};

#include "Systems/LightSystem.hpp"
#include "Systems/RenderSystem.hpp"

class ScriptSystem
{
public:
	void Start(World& world)
	{
		world.Each<ScriptComponent>([](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b) continue;
					b->SyncEnableState();
					b->EnsureStarted();
				}
			});
	}

	void Update(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b) continue;
					// enabled の切り替わりはここで拾う。
					// 無効側でも呼ぶのは OnDisable を落とさないため
					b->SyncEnableState();
					if (!b->enabled) continue;
					b->EnsureStarted();          // 遅延生成された分をここで拾う
					b->TickInvokes(deltatime);   // 予約された処理を先に消化する
					b->OnUpdate(deltatime);
				}
			});
	}

	/// @brief ポーズ中の更新。runDuringPause を立てたものだけ回す
	void UpdateDuringPause(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b || !b->enabled) continue;
					b->EnsureStarted();   // ポーズ中にロードされた場合もここで拾う
					if (!b->runDuringPause) continue;
					b->SyncEnableState();
					b->TickInvokes(deltatime);
					b->OnUpdate(deltatime);
				}
			});
	}

	void FixedUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b || !b->enabled) continue;
					b->OnFixedUpdate(deltatime);
				}
			});
	}

	void LateUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b || !b->enabled) continue;
					b->OnLateUpdate(deltatime);
				}
			});
	}

	void Draw(World& world, const RenderContext& context)
	{
		world.Each<ScriptComponent>([&context](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b || !b->enabled) continue;
					b->OnDraw(context);
				}
			});
	}

	/// @brief 破棄予約されている Entity の OnDestroy を呼ぶ
	/// @note World::FlushDestroyQueue の直前に呼ぶこと。
	///       実際に消えたあとでは behaviors ごと無くなっている
	void NotifyPendingDestroy(World& world)
	{
		if (!world.HasPendingDestroy()) return;

		world.Each<ScriptComponent>([&world](Entity e, ScriptComponent& sc)
			{
				if (world.IsPendingDestroy(e) == false) return;
				for (auto& b : sc.behaviors)
				{
					if (!b) continue;
					b->OnDestroy();
				}
			});
	}
};

class SpriteRenderSystem
{
public:
	void Draw(
		_In_ World& world,
		_In_ const RenderContext& renderContext,
		_In_ Mesh& quad,
		_In_ Material& spriteMaterial)
	{
		if (renderContext.CommandList == nullptr)
		{
			return;
		}

		world.Each<TransformComponent, SpriteComponent>([&](Entity, TransformComponent& transform, SpriteComponent& sprite)
			{

				// material 未生成 & パスあり
				if (!sprite.material && !sprite.texturePath.empty())
				{
					auto mat = std::make_shared<Material>();
					mat->Init();
					mat->SetTextureFromFile(sprite.texturePath);
					sprite.material = mat;
				}

				// material 未生成なら描画しない
				if (!sprite.material) return;

				transform.ApplyEuler();
				sprite.material->UpdateTextureIfNeeded(renderContext.CommandList);

				sprite.material->Apply(renderContext.CommandList,
					transform.world,renderContext.view,renderContext.projection,
					renderContext.wireframe,renderContext.frameIndex);

				quad.Draw(renderContext.CommandList);
			});
	}
};

class FreeLookSystem
{
public:
	void Update(World& world, float deltatime,CameraComponent::CameraType targetType)
	{
		// ビューポート上で右クリック中のみカメラ操作＆カーソルロック
		const bool active = INPUT->MouseInput.Right().IsPressed()
			&& INPUT->IsViewportHovered();

		if (active && !m_CursorHidden)
		{
			INPUT->SetCursorLock(true);
			INPUT->ShowCursor(false);
			m_CursorHidden = true;
		}
		else if (!active && m_CursorHidden)
		{
			INPUT->SetCursorLock(false);
			INPUT->ShowCursor(true);
			m_CursorHidden = false;
		}

		world.Each<TransformComponent, FreeLookComponent>(
			[&](Entity entity, TransformComponent& tr, FreeLookComponent& fl)
			{
				// FreeLookが有効でない場合処理しない
				if (!fl.Enabled) return;

				// このEntityのカメラ種別が対象でなければスキップ
				if (!world.HasComponent<CameraComponent>(entity)) return;
				if (world.GetComponent<CameraComponent>(entity).cameraType != targetType) return;

				if (!active) return;

				// 回転
				fl.yaw += (float)INPUT->MouseInput.DeltaX() * fl.rotateSpeed;
				fl.pitch += (float)INPUT->MouseInput.DeltaY() * fl.rotateSpeed;
				fl.pitch = std::clamp(fl.pitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);

				vector q = DirectX::XMQuaternionRotationRollPitchYaw(fl.pitch, fl.yaw, 0.0f);
				DirectX::XMStoreFloat4(&tr.rotation, DirectX::XMQuaternionNormalize(q));

				// 移動（WASD）
				float speed = 1.0f;
				vector move = DirectX::XMVectorZero();
				if (INPUT->Key.Shift().IsPressed()) speed *= 5.0f;
				if (INPUT->Key.W().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0, 0, speed, 0));
				if (INPUT->Key.S().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0, 0, -speed, 0));
				if (INPUT->Key.A().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(-speed, 0, 0, 0));
				if (INPUT->Key.D().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(speed, 0, 0, 0));

				if (!DirectX::XMVector3Equal(move, DirectX::XMVectorZero()))
				{
					move = DirectX::XMVector3Normalize(move);
					move = DirectX::XMVectorScale(move, fl.moveSpeed * deltatime);
					vector pos = XMLoadFloat3(&tr.position);
					pos = DirectX::XMVectorAdd(pos, DirectX::XMVector3Rotate(move, q));
					DirectX::XMStoreFloat3(&tr.position, pos);
				}
			});
	}
private:
	bool m_CursorHidden = false;
};

class CameraSystem
{
public:
	void Update(World& world, float aspect)
	{
		world.Each<TransformComponent,CameraComponent>([aspect]
		(Entity, TransformComponent& tr, CameraComponent& camera) {
			if (!camera.isActive)
			{
				return;
			}

			DirectX::XMMATRIX w = DirectX::XMLoadFloat4x4(&tr.world);

			// スケール除去
			DirectX::XMVECTOR s, q, t;
			if(!DirectX::XMMatrixDecompose(&s, &q, &t, w))
			{
				// 失敗時はローカル値へ
				q = DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&tr.rotation));
				t = DirectX::XMLoadFloat3(&tr.position);
			}

			vector forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);
			vector up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), q);
			DirectX::XMStoreFloat4x4(&camera.view, DirectX::XMMatrixLookToLH(t, forward, up));

			matrix p = (camera.projection == CameraComponent::Projection::Perspective)
				? DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(camera.fovY), aspect, camera.nearZ, camera.farZ)
				: DirectX::XMMatrixOrthographicLH(camera.orhoSize * aspect, camera.orhoSize, camera.nearZ, camera.farZ);
			DirectX::XMStoreFloat4x4(&camera.proj, p);
			});
	}
};

class CameraAnimationSystem
{
public:
	void Update(World& world, float deltatime, bool isPlaying)
	{
		world.Each<TransformComponent, CameraComponent, CameraAnimationComponent>(
			[&](Entity, TransformComponent& tr, CameraComponent& cam, CameraAnimationComponent& anim)
			{
				if (!anim.loaded && !anim.vmdPath.empty())
				{
					anim.clip = ModelLoader::LoadVMDCameraClip(anim.vmdPath);
					anim.loaded = true;
				}
				if (!anim.playing || !isPlaying || anim.clip.keys.empty()) return;

				anim.time += deltatime;
				if (anim.time > anim.clip.duration)
					anim.time = anim.loop ? std::fmod(anim.time, anim.clip.duration) : anim.clip.duration;

				// 前後キーを検索して線形補間
				const auto& keys = anim.clip.keys;
				auto it = std::lower_bound(keys.begin(), keys.end(), anim.time,
					[](const CameraKeyFrame& k, float t) { return k.time < t; });
				CameraKeyFrame k;
				if (it == keys.begin())      k = keys.front();
				else if (it == keys.end())   k = keys.back();
				else
				{
					const auto& k1 = *it; const auto& k0 = *(it - 1);
					// MMDのカット切替(同フレーム or 1フレーム差)は補間しない
					const float span = k1.time - k0.time;
					float t = (span <= 1.0f / 30.0f + 1e-4f) ? 0.0f
						: (anim.time - k0.time) / span;
					k.distance = std::lerp(k0.distance, k1.distance, t);
					k.target = { std::lerp(k0.target.x, k1.target.x, t),
								   std::lerp(k0.target.y, k1.target.y, t),
								   std::lerp(k0.target.z, k1.target.z, t) };
					k.rotation = { std::lerp(k0.rotation.x, k1.rotation.x, t),
								   std::lerp(k0.rotation.y, k1.rotation.y, t),
								   std::lerp(k0.rotation.z, k1.rotation.z, t) };
					k.fovY = std::lerp(k0.fovY, k1.fovY, t);
				}

				using namespace DirectX;
				XMVECTOR q = XMQuaternionRotationRollPitchYaw(-k.rotation.x, k.rotation.y, k.rotation.z);
				XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
				XMVECTOR eye = XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&k.target),
					XMVectorScale(fwd, k.distance));

				XMStoreFloat3((XMFLOAT3*)&tr.position, eye);
				XMStoreFloat4((XMFLOAT4*)&tr.rotation, q);   // Transformの回転がクォータニオンの場合
				cam.fovY = k.fovY;
			});
	}
};

class NameSytem
{
public:
	static std::string GetName(World& world, Entity entity)
	{
		world.Each<NameComponent>([&](Entity e, NameComponent& name)
			{
				if (e == entity)
				{
					return name.name;
				}
			});
		return std::to_string(entity);
	}

	static void SetName(World& world, Entity entity, const std::string& name)
	{
		// -------------------------//
		// 同じ名前が存在してる場合	//
		// 名前 + _番号にする		//
		// -------------------------//
		std::string newName = GenerateName(world, entity, name, 1);
		if (world.HasComponent<NameComponent>(entity))
			world.GetComponent<NameComponent>(entity).name = newName;   // 書き戻す
		else
			world.AddComponent<NameComponent>(entity, NameComponent{ newName });
	}

	/// @brief 名前を捜索する
	static std::string GenerateName(World& world, Entity entity,const std::string& name,int num)
	{
		std::string result = name;
		bool conflict = true;
		while (conflict)
		{
			conflict = false;
			world.Each<NameComponent>([&](Entity e, NameComponent& nameComp)
				{
					// 自分自身は除外
					if(e != entity && nameComp.name == result)
					{
						conflict = true;
					}
				});
			if (conflict)
			{
				result = name + "_" + std::to_string(num++);
			}
		}

		return result;
	}
};

/// @brief UIボタンのホバー / クリック判定
/// @note CanvasRenderSystem と同じ矩形の求め方をしている。
///       あちらを変えたらこちらも合わせること
class UIButtonSystem
{
public:
	/// @param screenW / screenH 描画に使っているスクリーンサイズ
	/// @param mouseX / mouseY ビューポート左上を原点としたマウス座標
	/// @param enabled 押下を受け付けるか(エディタでゲーム画面が非表示のときなど)
	void Update(
		_In_ World& world,
		_In_ float screenW,
		_In_ float screenH,
		_In_ float mouseX,
		_In_ float mouseY,
		_In_ bool enabled)
	{
		(void)screenW;
		(void)screenH;

		const bool down = enabled && INPUT->GetMouseButtonDown(0);
		const bool held = enabled && INPUT->GetMouseButton(0);
		const bool up = enabled && INPUT->GetMouseButtonUp(0);

		// クリック確定は走査の外で呼ぶ。
		// onClick がエンティティを増減させると Each が壊れるため
		std::vector<std::function<void()>> fired;

		world.Each<RectTransformComponent, UIButtonComponent>(
			[&](Entity e, RectTransformComponent& rt, UIButtonComponent& btn)
			{
				// 非表示のボタンは押せない(ポーズメニューを隠している間など)
				const bool hidden =
					world.HasComponent<UIImageComponent>(e) &&
					!world.GetComponent<UIImageComponent>(e).visible;

				if (!btn.interactable || hidden)
				{
					btn.isHovered = false;
					btn.isPressed = false;
					return;
				}

				// CanvasRenderSystem と同じ: AnchoredPosition が左上、SizeDelta が大きさ
				const float x0 = rt.AnchoredPosition.x;
				const float y0 = rt.AnchoredPosition.y;
				const float x1 = x0 + rt.SizeDelta.x;
				const float y1 = y0 + rt.SizeDelta.y;

				const bool inside =
					enabled &&
					mouseX >= x0 && mouseX <= x1 &&
					mouseY >= y0 && mouseY <= y1;

				btn.isHovered = inside;

				// 状態色を UIImage へ渡す。基準色(color)は残したまま掛け合わせる
				if (world.HasComponent<UIImageComponent>(e))
				{
					auto& img = world.GetComponent<UIImageComponent>(e);
					const COLOR st = StateColor(btn);
					img.runtimeColor = COLOR{
						img.color.x * st.x, img.color.y * st.y,
						img.color.z * st.z, img.color.w * st.w };
				}

				if (inside && down)
				{
					btn.isPressed = true;
				}
				else if (!held)
				{
					// 押したまま外へ出て離した場合はクリック扱いにしない
					if (btn.isPressed && inside && up && btn.onClick)
					{
						fired.push_back(btn.onClick);
					}
					btn.isPressed = false;
				}
			});

		for (auto& fn : fired)
		{
			fn();
		}
	}

	/// @brief 状態に応じた色を返す(UIImage の色に掛ける)
	static COLOR StateColor(const UIButtonComponent& btn)
	{
		if (!btn.interactable) return btn.disabledColor;
		if (btn.isPressed)     return btn.pressedColor;
		if (btn.isHovered)     return btn.hoverColor;
		return btn.normalColor;
	}
};

class CanvasRenderSystem
{
public:
	void Draw(
		_In_ World& world,
		_In_ const RenderContext& ctx,
		_In_ Mesh& quad,
		_In_ ID3D12PipelineState* uiPso,
		_In_ float screenW,
		_In_ float screenH)
	{
		// スクリーン正射影
		float4x4 view, proj;
		DirectX::XMStoreFloat4x4(&view, DirectX::XMMatrixIdentity());
		DirectX::XMStoreFloat4x4(&proj, DirectX::XMMatrixOrthographicOffCenterLH(
			0,screenW,screenH,0,0.0f,1.0f));

		world.Each<RectTransformComponent, UIImageComponent>(
			[&](Entity e, RectTransformComponent& rt, UIImageComponent& img)
			{
				if (!img.visible) return;

				// マテリアルの遅延生成。
				// テクスチャが無くても Init() の既定(2x2白)を色で塗って板として使う
				if (!img.material)
				{
					auto mat = std::make_shared<Material>();
					mat->Init();
					if (!img.texturePath.empty())
					{
						mat->SetTextureFromFile(Utf8ToWide(img.texturePath));
					}
					img.material = mat;
					img.uploadedColor = COLOR{ -1.0f, -1.0f, -1.0f, -1.0f };   // 次で必ず塗る
				}

				if (!img.material) return;

				// 単色運用のときだけ塗り替える。画像がある場合は色を掛けられない
				if (img.texturePath.empty())
				{
					// UIButton が無いエンティティは runtimeColor が更新されないので color を使う
					const COLOR& want = world.HasComponent<UIButtonComponent>(e)
						? img.runtimeColor : img.color;

					if (want.x != img.uploadedColor.x || want.y != img.uploadedColor.y ||
						want.z != img.uploadedColor.z || want.w != img.uploadedColor.w)
					{
						img.material->SetSolidColor(want);
						img.uploadedColor = want;
					}
				}

				// RectTransform -> ピクセル空間world座標
				// quad は左上座標 + 半サイズで中心に置く
				const float w = rt.SizeDelta.x;
				const float h = rt.SizeDelta.y;
				const float cx = rt.AnchoredPosition.x + w * 0.5f;
				const float cy = rt.AnchoredPosition.y + h * 0.5f;
				float4x4 worldf;
				DirectX::XMStoreFloat4x4(&worldf,
					DirectX::XMMatrixScaling(w, h, 1.0f) * DirectX::XMMatrixTranslation(cx, cy, 0.0f));

				img.material->Apply(
					ctx.CommandList, worldf, view, proj, false,
					ctx.frameIndex, ctx.cbAllocator,"",
					uiPso);

				quad.Draw(ctx.CommandList);
			});

		// text描画
		world.Each<RectTransformComponent, UITextComponent>(
			[&](Entity, RectTransformComponent& rt, UITextComponent& txt)
			{
				if (!txt.visible) return;

				FontAtlas* atlas = FontLibrary::Get(txt.fontPath);
				if (!atlas) return;

				if (!txt.mesh) txt.mesh = std::make_shared<Mesh>();

				// 文字が変わった時だけメッシュ再構築（fontSizeは含めない）
				if (txt.isDirty || txt.text != txt._lastText)
				{
					APP->WaitForGPUIdle();
					atlas->BuildTextMesh(*txt.mesh, txt.text, txt.color);
					txt._lastText = txt.text;
					txt.isDirty = false;
				}

				// fontSize はワールドのスケールで効かせる（再ベイク・再構築なし＝軽い）
				const float s = txt.fontSize / atlas->RefHeight();
				float4x4 worldf;
				DirectX::XMStoreFloat4x4(&worldf,
					DirectX::XMMatrixScaling(s, s, 1.0f) *
					DirectX::XMMatrixTranslation(rt.AnchoredPosition.x, rt.AnchoredPosition.y, 0.0f));

				atlas->GetMaterial().Apply(ctx.CommandList, worldf, view, proj, false,
					 ctx.frameIndex, ctx.cbAllocator,"",uiPso);
				txt.mesh->Draw(ctx.CommandList);
			});
	}
};

class AudioSystem
{
public:
	/// @param isPlaying 再生中か
	/// @param isPaused ポーズ中か
	/// @note 停止(EDITOR復帰)は音を捨てるが、ポーズは位置を保って止めるだけ。
	///       ライブ中に ESC で止めたとき曲が頭に戻らないようにするため
	void Update(World& world, bool isPlaying, bool isPaused = false)
	{
		//static int f = 0;
		//if ((f++ % 60) == 0)
		//{
		//	int srcCount = 0, listenerCount = 0;
		//	world.Each<AudioSourceComponent>([&](Entity, AudioSourceComponent&) { srcCount++; });
		//	world.Each<AudioListenerComponent>([&](Entity, AudioListenerComponent&) { listenerCount++; });

		//	char b[256];
		//	sprintf_s(b, "[Audio] Update playing=%d sources=%d listeners=%d\n",
		//		(int)isPlaying, srcCount, listenerCount);
		//	OutputDebugStringA(b);
		//}

		// ポーズ中は「再生していない」が「停止でもない」。
		// 停止と同じ扱いにすると曲が捨てられて頭から鳴り直しになる
		const bool justStarted = (isPlaying && !m_PrevPlaying && !m_PrevPaused);
		const bool justStopped = (!isPlaying && !isPaused && m_PrevPlaying);
		const bool justPaused = (isPaused && !m_PrevPaused);
		const bool justResumed = (!isPaused && m_PrevPaused);

		m_PrevPlaying = isPlaying;
		m_PrevPaused = isPaused;

		// ---- リスナー（耳）を1つ探す ---- //
		X3DAUDIO_LISTENER listener{};
		bool hasListener = false;
		world.Each<AudioListenerComponent, TransformComponent>(
			[&](Entity, AudioListenerComponent&, TransformComponent& tr)
			{
				if (hasListener) return;
				hasListener = true;

				listener.Position = { tr.position.x, tr.position.y, tr.position.z };

				// 向き（回転クォータニオンからforward/upを出す）
				using namespace DirectX;
				XMVECTOR q = XMLoadFloat4(&tr.rotation);
				XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
				XMVECTOR up = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q);
				XMFLOAT3 f, u; XMStoreFloat3(&f, fwd); XMStoreFloat3(&u, up);
				listener.OrientFront = { f.x, f.y, f.z };
				listener.OrientTop = { u.x, u.y, u.z };
			});

		world.Each<AudioSourceComponent, TransformComponent>(
			[&](Entity, AudioSourceComponent& src, TransformComponent& tr)
			{
				// ---- ロード ---- //
				if (!src.clip && !src.clipPath.empty())
				{
					// 絶対パスで保存されたシーンでも Assets の中なら拾えるようにする
					src.clip = AudioEngine::Get().Load(ResolveAssetPath(src.clipPath));
					if (src.clip)
						src.voice = AudioEngine::Get().CreateVoice(src.clip->format);
				}
				if (!src.voice || !src.clip) return;

				// ---- playOnStart / 再生・停止（前回と同じ） ---- //
				if (justStarted && src.playOnStart) src.playRequested = true;
				if (justStopped) src.stopRequested = true;
				if (justPaused)  src.pauseRequested = true;
				if (justResumed) src.resumeRequested = true;

				if (src.playRequested)
				{
					src.playRequested = false;
					src.voice->Stop();
					src.voice->FlushSourceBuffers();
					XAUDIO2_BUFFER buf{};
					buf.AudioBytes = (UINT32)src.clip->data.size();
					buf.pAudioData = src.clip->data.data();
					buf.Flags = XAUDIO2_END_OF_STREAM;
					buf.LoopCount = src.loop ? XAUDIO2_LOOP_INFINITE : 0;
					src.voice->SubmitSourceBuffer(&buf);
					src.voice->Start();
					src.isPlaying = true;
				}
				if (src.stopRequested)
				{
					src.stopRequested = false;
					src.voice->Stop();
					src.voice->FlushSourceBuffers();
					src.isPlaying = false;
				}

				// ---- シーク(PlayBeginでバッファを投げ直す) ---- //
				if (src.seekRequested)
				{
					src.seekRequested = false;

					const WAVEFORMATEX& fmt = src.clip->format;
					const UINT32 blockAlign = (fmt.nBlockAlign > 0) ? fmt.nBlockAlign : 1;
					const UINT32 totalSamples = (UINT32)(src.clip->data.size() / blockAlign);

					UINT32 sampleOffset = (UINT32)(std::max(0.0f, src.seekSeconds) * fmt.nSamplesPerSec);
					if (totalSamples == 0) sampleOffset = 0;
					else if (sampleOffset >= totalSamples) sampleOffset = totalSamples - 1;

					src.voice->Stop();
					src.voice->FlushSourceBuffers();

					XAUDIO2_BUFFER buf{};
					buf.AudioBytes = (UINT32)src.clip->data.size();
					buf.pAudioData = src.clip->data.data();
					buf.Flags = XAUDIO2_END_OF_STREAM;
					buf.LoopCount = src.loop ? XAUDIO2_LOOP_INFINITE : 0;
					buf.PlayBegin = sampleOffset;   // ここから再生
					src.voice->SubmitSourceBuffer(&buf);

					// 止まっていたならバッファを積むだけ。
					// ここで無条件に Start すると、スライダーを動かしただけで鳴り出す
					if (src.isPlaying) src.voice->Start();
				}

				// ---- 一時停止 / 再開(位置は保持される) ---- //
				if (src.pauseRequested) { src.pauseRequested = false; src.voice->Stop(); src.isPlaying = false; }
				if (src.resumeRequested) { src.resumeRequested = false; src.voice->Start(); src.isPlaying = true; }

				// ---- 3D定位 ---- //
				if (src.is3D && hasListener)
				{
					X3DAUDIO_EMITTER emitter{};
					emitter.Position = { tr.position.x, tr.position.y, tr.position.z };
					emitter.OrientFront = { 0, 0, 1 };
					emitter.OrientTop = { 0, 1, 0 };
					emitter.ChannelCount = 1;                 // モノラル前提（3D音源は基本モノラル）
					emitter.CurveDistanceScaler = src.maxDistance;
					emitter.DopplerScaler = 1.0f;

					const UINT32 outCh = AudioEngine::Get().OutputChannels();
					float matrix[8] = {};                     // 最大8chスピーカー分
					X3DAUDIO_DSP_SETTINGS dsp{};
					dsp.SrcChannelCount = 1;
					dsp.DstChannelCount = outCh;
					dsp.pMatrixCoefficients = matrix;

					X3DAudioCalculate(
						AudioEngine::Get().X3D(), &listener, &emitter,
						X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER,
						&dsp);

					// 出力マトリクス（定位＋距離減衰）とドップラーを反映
					src.voice->SetOutputMatrix(nullptr, 1, outCh, matrix);
					src.voice->SetFrequencyRatio(dsp.DopplerFactor);
					src.voice->SetVolume(src.volume);
				}
				else
				{
					// 2D（従来通り）
					src.voice->SetVolume(src.volume);
				}
			});
	}
private:
	bool m_PrevPlaying = false;
	bool m_PrevPaused = false;
};

class TransformSystem
{
public:
	void Update(World& world)
	{
		// 各エンティティのworldを、親をたどって計算
		m_Done.clear();
		world.Each<TransformComponent>([&](Entity e, TransformComponent& tr)
			{
				UpdateWorld(world, e, tr);
			});
	}

private:
	void UpdateWorld(World& world, Entity e, TransformComponent& tr)
	{
		using namespace DirectX;

		// 子から親を何度もたどるので、このフレームで計算済みならスキップ
		if (!m_Done.insert(e).second) return;

		XMMATRIX local =
			XMMatrixScaling(tr.scale.x, tr.scale.y, tr.scale.z) *
			XMMatrixRotationQuaternion(XMVector4Normalize(XMLoadFloat4(&tr.rotation))) *
			XMMatrixTranslation(tr.position.x, tr.position.y, tr.position.z);

		XMMATRIX worldMat = local;
		// 親が自分自身でなく、実在する場合のみ
		if (tr.parent != INVALID_ENTITY && tr.parent != e &&
			world.HasComponent<TransformComponent>(tr.parent))
		{
			auto& p = world.GetComponent<TransformComponent>(tr.parent);
			UpdateWorld(world, tr.parent, p);
			worldMat = local * XMLoadFloat4x4(&p.world);
		}
		XMStoreFloat4x4(&tr.world, worldMat);
	}

	std::unordered_set<Entity> m_Done;	///< このフレームで world を計算済みのエンティティ
};

class ShadowSystem
{
public:
	void Draw(
		World& world, 
		const RenderContext& ctx,
		ID3D12PipelineState* shadowPso)
	{
		if(!shadowPso || !ctx.CommandList || !ctx.cbAllocator)
		{
			return;
		}
		auto* cmd = ctx.CommandList;
		cmd->SetPipelineState(shadowPso);
		cmd->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const UINT slot = ctx.frameIndex % RTV_NUM;

		auto b2 = ctx.cbAllocator->Allocate(slot, &ctx.lightCb, sizeof(LightCB));
		if (b2)
		{
			cmd->SetGraphicsRootConstantBufferView(2, b2);
		}

		// スキン無しエンティティで共有する単位行列パレット。パスごとに1回だけ転送する
		const D3D12_GPU_VIRTUAL_ADDRESS identityBoneVA =
			ctx.cbAllocator->Allocate(slot, &IdentityBoneCB(), sizeof(BoneCB));

		world.Each<TransformComponent, MeshComponent>(
			[&](Entity e, TransformComponent& tr, MeshComponent& mc)
			{
				if (!mc.mesh) return;

				// スキンメッシュ用の骨パレット。渡さないとキャラの影が
				// バインドポーズのまま固まる
				{
					const size_t n = world.HasComponent<AnimatorComponent>(e)
						? std::min<size_t>(world.GetComponent<AnimatorComponent>(e).palette.size(), MAX_BONES)
						: 0;
					D3D12_GPU_VIRTUAL_ADDRESS b4 = identityBoneVA;
					if (n > 0)
					{
						BoneCB bone{};
						const auto& palette = world.GetComponent<AnimatorComponent>(e).palette;
						for (size_t i = 0; i < n; ++i) bone.boneMatrices[i] = palette[i];
						for (size_t i = n; i < MAX_BONES; ++i)
							DirectX::XMStoreFloat4x4(&bone.boneMatrices[i], DirectX::XMMatrixIdentity());
						b4 = ctx.cbAllocator->Allocate(slot, &bone, sizeof(BoneCB));
					}
					if (b4) cmd->SetGraphicsRootConstantBufferView(5, b4);
				}

				struct { float4x4 world; } obj{};
				const auto w = DirectX::XMLoadFloat4x4(&tr.world);
				DirectX::XMStoreFloat4x4(&obj.world, DirectX::XMMatrixTranspose(w));

				auto b1 = ctx.cbAllocator->Allocate(slot, &obj, sizeof(obj));
				if (b1) cmd->SetGraphicsRootConstantBufferView(1, b1);
				mc.mesh->Draw(cmd);
			});
	}
};

class AnimatorSystem
{
public:
	void Update(World& world, float dt)
	{
		world.Each<AnimatorComponent>([&](Entity e, AnimatorComponent& an)
			{
				using clk = std::chrono::high_resolution_clock;
				auto t0 = clk::now();

				// Play/Stop で退避したクリップがあれば、読み直さずに拾い直す
				if (!an.clipsRestored && !an.skeleton.nodes.empty() &&
					world.HasComponent<NameComponent>(e))
				{
					auto& cache = AnimatorClipCache();
					const auto it = cache.find(world.GetComponent<NameComponent>(e).name);
					if (it != cache.end() && !it->second.clips.empty())
					{
						an.clips = std::move(it->second.clips);
						an.currentClip = it->second.currentClip;
						an.currentClipName = it->second.currentClipName;
						an.time = it->second.time;
						an.playing = it->second.playing;
						an.clipsRestored = true;   // 非同期ロードで二重に積まない
						an.physicsResetRequest = true;
						cache.erase(it);
					}
				}

				// 保存されたVMDパスからクリップを復元(スケルトン準備後に1回だけ)
				if (!an.clipsRestored && !an.clipPathsStr.empty() &&
					!an.skeleton.nodes.empty())
				{
					an.clipsRestored = true;
					std::stringstream ss(an.clipPathsStr);
					std::string path;
					std::vector<std::string> seen;   // 重複したパスは1回だけ読む
					while (std::getline(ss, path, '|'))
					{
						if (path.empty()) continue;
						if (std::find(seen.begin(), seen.end(), path) != seen.end()) continue;
						seen.push_back(path);
						// .vmd はスケルトンに合わせて読み、.fbx などは中のクリップを全部足す
						const bool isVmd = path.size() > 4 &&
							_stricmp(path.c_str() + path.size() - 4, ".vmd") == 0;

						if (isVmd)
						{
							AsyncLoader::Get().LoadVMDAsync(path, an.skeleton,
								[&world, e](AnimationClip vc)
								{
									if (vc.channels.empty() && vc.morphChannels.empty()) return;
									if (!world.IsEntityAlive(e) ||
										!world.HasComponent<AnimatorComponent>(e)) return;
									auto& a = world.GetComponent<AnimatorComponent>(e);
									a.clips.push_back(std::move(vc));
								});
						}
						else
						{
							AsyncLoader::Get().LoadAnimationFileAsync(path,
								[&world, e](std::vector<AnimationClip> clips)
								{
									if (!world.IsEntityAlive(e) ||
										!world.HasComponent<AnimatorComponent>(e)) return;
									auto& a = world.GetComponent<AnimatorComponent>(e);
									for (auto& c : clips)
									{
										if (c.channels.empty() && c.morphChannels.empty()) continue;
										a.clips.push_back(std::move(c));
									}
								});
						}
					}
				}

				// ---- モーフオフセットの確定 ---- //
				// 以前は RenderSystem::Draw の中で再計算していたが、
				// 描画から World を書き換えることになり Game/Render を分けられない。
				// クリップが無くてもモーフだけ動かすケースがあるので、
				// 下の early return より前で処理する
				if (an.morphDirty)
				{
					size_t vcount = 0;
					if (world.HasComponent<MeshComponent>(e))
					{
						const auto& mc = world.GetComponent<MeshComponent>(e);
						if (mc.mesh) vcount = mc.mesh->GetVertexCount();
					}

					if (vcount > 0)
					{
						RebuildMorphOffsets(an.morphs, an.morphWeights, vcount, an.morphoffsets);
					}
					an.morphDirty = false;
				}

				if (an.clips.empty()) return;

				// 保存された名前を正として添字を引き直す。
				// クリップは非同期に届くので、並び順は毎回同じとは限らない
				if (!an.currentClipName.empty() &&
					(an.currentClip < 0 || an.currentClip >= (int)an.clips.size() ||
						an.clips[an.currentClip].name != an.currentClipName))
				{
					for (int i = 0; i < (int)an.clips.size(); ++i)
					{
						if (an.clips[i].name == an.currentClipName)
						{
							an.currentClip = i;
							break;
						}
					}
				}

				if (an.currentClip < 0 || an.currentClip >= (int)an.clips.size()) return;
				const AnimationClip& clip = an.clips[an.currentClip];
				if (an.playing && clip.duration > 0.0f)
				{
					an.time += dt * an.speed;
					if (an.loop)
					{
						an.time = fmodf(an.time, clip.duration);
						if (an.time < 0.0f) an.time += clip.duration;
					}
					else if (an.time >= clip.duration)
					{
						an.time = clip.duration;
						an.playing = false;
					}
				}

				MmdPhysics* phys = nullptr;
				if (world.HasComponent<MmdPhysicsComponent>(e))
					phys = world.GetComponent<MmdPhysicsComponent>(e).impl.get();

				// ---- 揺れもの(Kawaii Physics) ---- //
				KawaiiPhysics* kawaii = nullptr;
				const KawaiiPhysicsSettings* kawaiiSettings = nullptr;
				const float4x4* entityWorld = nullptr;
				if (world.HasComponent<KawaiiPhysicsComponent>(e))
				{
					auto& kp = world.GetComponent<KawaiiPhysicsComponent>(e);
					if (!kp.impl) kp.impl = std::make_shared<KawaiiPhysics>();

					// シーンから読んだ設定文字列を一度だけ展開する
					if (!kp.configRestored)
					{
						KawaiiDeserialize(kp.configStr, kp.settings);
						kp.impl->MarkDirty();
						kp.configRestored = true;
					}
					// シーク・スクラブ中は慣性を持ち込ませない
					if (an.physicsResetRequest || an.scrubbing) kp.impl->RequestResync();

					if (kp.enabled)
					{
						kawaii = kp.impl.get();
						kawaiiSettings = &kp.settings;
					}
				}
				if (world.HasComponent<TransformComponent>(e))
					entityWorld = &world.GetComponent<TransformComponent>(e).world;

				// シーク直後は剛体を現在のボーン姿勢へ再同期(爆発防止)
				if (an.physicsResetRequest)
				{
					if (phys) phys->Reset();
					an.physicsResetRequest = false;
				}

				// 揺れものは Kawaii Physics へ全面移行したので MmdPhysics は渡さない
				// スライダーをドラッグしている間はFK/IKのみ(物理を進めない)
				ComputePalette(an.skeleton, an.skinData, clip, an.time, an.palette,
					nullptr, dt,
					an.scrubbing ? nullptr : kawaii, kawaiiSettings, entityWorld);

				// 表情モーフをVMDから駆動する。
				// morphClip が有効ならそちらを使う(体と表情でVMDが別のケース)
				if (!an.morphs.morphs.empty())
				{
					const int mi = (an.morphClip >= 0 && an.morphClip < (int)an.clips.size())
						? an.morphClip : an.currentClip;

					std::vector<std::string> missing;
					if (SampleMorphWeights(an.clips[mi], an.time, an.morphs,
						an.morphWeights, &missing))
					{
						an.morphDirty = true;
					}

					// 解決できなかったモーフ名は一度だけ報告する
					if (!missing.empty())
					{
						static std::unordered_set<std::string> warned;
						for (const auto& n : missing)
						{
							if (warned.insert(n).second)
								LOG->LogInfo("morph not found in model: " + n);
						}
					}
				}

				auto t1 = clk::now();
				static int c = 0;
				if ((c++ % 60) == 0) {
					double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
					LOG->LogInfo("Anim+Physics: " + std::to_string(ms) + " ms");
				}
			});
	}
};

class ParticleSystem
{
public:
	void Update(World& world, float dt)
	{
		world.Each<TransformComponent, ParticleEmitterComponent>(
			[&](Entity e, TransformComponent& tr, ParticleEmitterComponent& pe)
			{
				float coneLen = 3.0f, coneRad = 0.3f;
				float3 origin = tr.position;
				DirectX::XMVECTOR dirV = DirectX::XMVector3Rotate(
					DirectX::XMVectorSet(0, 0, 1, 0),
					DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&tr.rotation)));

				if (pe.followLight && world.HasComponent<LightComponent>(e))
				{
					const auto& light = world.GetComponent<LightComponent>(e);
					coneLen = light.range;
					if (light.type == LightComponent::LightType::Laser)
						coneRad = 0.2f;
					else
						coneRad = tanf(DirectX::XMConvertToRadians(light.spotAngle * 0.5f)) * coneLen;

					dirV = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&light.direction));
				}

				float3 dir;
				DirectX::XMStoreFloat3(&dir, dirV);

				// --- 既存パーティクルの更新 ---
				for (auto& p : pe.particles)
				{
					p.age += dt;
					p.pos = p.pos + p.velocity * dt + pe.gravity * dt;
				}
				// 寿命切れを削除（swap-and-pop、再アロケーションなし）
				pe.particles.erase(
					std::remove_if(pe.particles.begin(), pe.particles.end(),
						[](const ParticleEmitterComponent::Particle& p) { return p.age >= p.life; }),
					pe.particles.end());

				// --- 新規発生 ---
				if (pe.emitting && pe.emitRate > 0.0f)
				{
					pe.spawnAccumulator += dt * pe.emitRate;
					while (pe.spawnAccumulator >= 1.0f &&
						static_cast<int>(pe.particles.size()) < pe.maxParticles)
					{
						pe.spawnAccumulator -= 1.0f;
						SpawnInCone(pe, origin, dir, coneLen, coneRad);
					}
				}
			});
	}

private:
	void SpawnInCone(ParticleEmitterComponent& pe, const float3& origin,
		const float3& dir, float len, float radius)
	{
		auto rnd = []() { return (float)rand() / RAND_MAX; };

		const float t = rnd();        
		const float rimR = radius * t;
		const float ang = rnd() * DirectX::XM_2PI;
		const float rr = sqrtf(rnd()) * rimR;

		DirectX::XMVECTOR dV = DirectX::XMLoadFloat3(&dir);
		float3 up = (fabsf(dir.y) > 0.99f) ? float3{ 1,0,0 } : float3{ 0,1,0 };
		DirectX::XMVECTOR rV = DirectX::XMVector3Normalize(
			DirectX::XMVector3Cross(dV, DirectX::XMLoadFloat3(&up)));
		DirectX::XMVECTOR uV = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(dV, rV));

		float3 r_, u_;
		DirectX::XMStoreFloat3(&r_, rV);
		DirectX::XMStoreFloat3(&u_, uV);

		ParticleEmitterComponent::Particle np{};
		np.pos = origin
			+ float3{ dir.x * len * t, dir.y * len * t, dir.z * len * t }
		+ r_ * (cosf(ang) * rr) + u_ * (sinf(ang) * rr);

		const float spd = pe.speed + (rnd() * 2.0f - 1.0f) * pe.speedVariance;

		np.velocity = float3{ dir.x * spd, dir.y * spd, dir.z * spd }
			+ r_ * ((rnd() * 2.0f - 1.0f) * pe.drift)
			+ u_ * ((rnd() * 2.0f - 1.0f) * pe.drift);

		np.life = pe.lifeTime + (rnd() * 2.0f - 1.0f) * pe.lifeTimeVariance;
		np.age = 0.0f;

		pe.particles.push_back(np);
	}
};

class MusicSyncSystem
{
public:
	void Update(World& world, bool isPlaying)
	{
		// isPlaying は見ない。音源が実際に鳴っているかどうかだけで判断するので、
		// エディタで曲を流している間も musicTime が進む
		(void)isPlaying;

		world.Each<AudioSourceComponent, MusicSyncComponent>(
			[&](Entity, AudioSourceComponent& src, MusicSyncComponent& sync)
			{
				if (!src.voice || !src.clip)
				{
					sync.started = false;
					return;
				}

				// シーク直後は再生開始点を取り直す
				if (sync.resyncRequested)
				{
					sync.resyncRequested = false;
					sync.started = false;
				}

				XAUDIO2_VOICE_STATE st{};
				src.voice->GetState(&st);

				// 再生開始の瞬間のSamplesPlayedを基準として記録
				// (voiceは過去の再生分もカウントし続けるため)
				if (!sync.started)
				{
					if (st.BuffersQueued == 0) return;  // まだ再生が始まっていない
					sync.startSamples = st.SamplesPlayed;
					sync.started = true;
				}
				if (st.BuffersQueued == 0) return;      // 再生終了

				const float rate = (float)src.clip->format.nSamplesPerSec;
				const float musicTime =
					(float)(st.SamplesPlayed - sync.startSamples) / rate
					+ sync.seekBase + sync.offset;

				sync.musicTime = musicTime;

				// 曲位置を正として時刻を上書き
				if (sync.syncAnimators)
				{
					world.Each<AnimatorComponent>(
						[&](Entity, AnimatorComponent& an)
						{
							if (an.playing) an.time = musicTime;
						});
				}
				if (sync.syncCamera)
				{
					world.Each<CameraAnimationComponent>(
						[&](Entity, CameraAnimationComponent& ca)
						{
							if (ca.playing) ca.time = musicTime;
						});
				}
			});
	}
};

class FollowCameraSystem
{
public:
	void Update(World& world, float deltatime, bool isPlaying)
	{
		using namespace DirectX;
		if (deltatime <= 0.0f) return;

		// ゲームとして起動したとき(ImGui が無い)は、カーソルを中央に固定して隠す。
		// そうしないと、ボタンを押していなくてもマウスを動かすだけで視点が回ってしまう
		const bool standalone = !IMGUI::IsInitialized();
		bool wantCapture = false;

		world.Each<TransformComponent, FollowCameraComponent>(
			[&](Entity e, TransformComponent& tr, FollowCameraComponent& fc)
			{
				if (!fc.enabled) return;

				// 対象をタグで探す
				Entity target = INVALID_ENTITY;
				world.Each<TagComponent>([&](Entity t, TagComponent& tag)
					{
						if (target == INVALID_ENTITY && tag.tag == fc.targetTag) target = t;
					});
				if (target == INVALID_ENTITY || !world.HasComponent<TransformComponent>(target)) return;

				if (isPlaying && fc.captureCursor && standalone) wantCapture = true;

				const bool canLook = isPlaying &&
					(standalone ? m_Captured
						: (INPUT->IsViewportHovered() && !INPUT->IsMouseCaptured()));

				// マウス : カーソルを掴んでいる間だけ
				if (canLook)
				{
					fc.yaw += (float)INPUT->MouseInput.DeltaX() * fc.rotateSpeed;
					fc.pitch += (float)INPUT->MouseInput.DeltaY() * fc.rotateSpeed;
				}

				if (isPlaying)
				{
					const float2 look = INPUT->GetVector("LookX", "LookY");

					fc.yaw += look.x * fc.padRotateSpeed * deltatime;

					// スティックは上が + 、pitch は下を向くと + なので既定で反転する
					const float pitchInput = fc.padInvertY ? look.y : -look.y;
					fc.pitch += pitchInput * fc.padRotateSpeed * deltatime;
				}

				fc.pitch = std::clamp(fc.pitch,
					XMConvertToRadians(fc.minPitch), XMConvertToRadians(fc.maxPitch));

				// 注視点 : 対象の少し上
				const auto& targetTr = world.GetComponent<TransformComponent>(target);
				const XMVECTOR focus = XMVectorAdd(
					XMLoadFloat3(&targetTr.position),XMVectorSet(0.0f,fc.height,0.0f,0.0f));

				// 注視点から、後ろ向きに distance だけ離れた位置
				const XMVECTOR rot = XMQuaternionRotationRollPitchYaw(fc.pitch, fc.yaw, 0.0f);
				const XMVECTOR back = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), rot);
				const XMVECTOR want = XMVectorAdd(focus, XMVectorScale(back, fc.distance));

				// 少し遅れて追いつく
				XMVECTOR pos = XMLoadFloat3(&tr.position);
				const float t = (fc.positionLag <= 0.0f)
					? 1.0f : 1.0f - std::exp(-fc.positionLag * deltatime);
				pos = XMVectorLerp(pos, want, t);

				XMStoreFloat3(&tr.position, pos);
				XMStoreFloat4(&tr.rotation, XMQuaternionNormalize(rot));
				tr.SyncEulerFromQuaternion();
				tr.RebuildWorld();
			});

		// Esc でカーソルを解放する(ウィンドウから出られなくなるのを避ける)
		if (m_Captured && INPUT->Key.Escape().IsPressed()) wantCapture = false;

		// 掴む / 離すは状態が変わった瞬間だけ。
		// ::ShowCursor は呼んだ回数を数えるので、毎フレーム呼ぶと戻らなくなる
		if (wantCapture != m_Captured)
		{
			m_Captured = wantCapture;
			INPUT->SetCursorLock(m_Captured);
			INPUT->ShowCursor(!m_Captured);
		}
	}

private:
	bool m_Captured = false;
};

class TerrainSystem
{
public:
	void Update(World& world)
	{
		world.Each<TerrainComponent>([&](Entity e, TerrainComponent& t)
			{
				if (t.BuiltSettings == Terrain::HashSettings(t) && !t.Heights.empty()) return;
				Terrain::Rebuild(world, e);
			});
	}
};
