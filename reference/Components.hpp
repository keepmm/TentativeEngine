#pragma once

#include "Defines.hpp"
#include "RenderContext.hpp"
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include "ScriptField.hpp"
#include "AudioEngine.hpp"
#include "ModelData.hpp"
#include "KawaiiPhysics.hpp"

class Mesh;
class Material;
class Camera;
class MonoBehavior;
class MmdPhysics;

namespace PhysX
{
	class PxRigidActor;
	class PxShape;
}

struct TransformComponent
{
	POSITION position{ 0.0f, 0.0f, 0.0f };
	QUATERNION rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	SCALE scale{ 1.0f,1.0f,1.0f };

	float3 EulerAngles{ 0.0f,0.0f,0.0f };

	Entity parent = INVALID_ENTITY;
	float4x4 world{};

	void Reflect(FieldList& f)
	{
		f.Add("Position", position);
		f.Add("Rotation", rotation);
		f.Add("Scale", scale);
	}

	/// @brief Euler -> Quaternion変換
	void ApplyEuler()
	{
		using namespace DirectX;
		const float pitch = XMConvertToRadians(EulerAngles.x);
		const float yaw = XMConvertToRadians(EulerAngles.y);
		const float roll = XMConvertToRadians(EulerAngles.z);
		XMVECTOR q = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
		XMStoreFloat4(&rotation, q);
	}

	/// @brief Quaternion -> Euler変換
	void SyncEulerFromQuaternion()
	{
		using namespace DirectX;
		XMFLOAT4 q = rotation;
		float sinp = 2.0f * (q.w * q.x - q.y * q.z);
		float pitch = (fabsf(sinp) >= 1.0f) ? copysignf(XM_PIDIV2, sinp) : asinf(sinp);
		float yaw = atan2f(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
		float roll = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.z * q.z + q.x * q.x));
		EulerAngles = { XMConvertToDegrees(pitch), XMConvertToDegrees(yaw), XMConvertToDegrees(roll) };
	}

	/// @brief positionの値を設定
	/// @param pos POSITION型の値 (x , y , x)
	void SetPosition(
		_In_ const POSITION& pos) { position = pos;}

	/// @brief positionの値を設定
	/// @param x float
	/// @param y float
	/// @param z float
	void SetPosition(
		_In_ float x, 
		_In_ float y, 
		_In_ float z) { position = POSITION{ x,y,z };}

	/// @brief positionの値を加算する
	/// @param delta POSITION型の値 (x , y , x)
	void Translate(
		_In_ const POSITION& delta) {
		position.x += delta.x; position.y += delta.y; position.z += delta.z;
	}

	/// @brief positionの値を加算する
	/// @param x float
	/// @param y float
	/// @param z float
	void Translate(
		_In_ float x,
		_In_ float y,
		_In_ float z) {
		position.x += x; position.y += y; position.z += z;
	}


	void RebuildWorld()
	{
		const matrix scaleMatrix = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);

		const auto rot = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rotation));
		DirectX::XMStoreFloat4(&rotation, rot);
		const matrix rotMatrix = DirectX::XMMatrixRotationQuaternion(rot);
		const matrix transMatrix = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
		const matrix worldMatrix = scaleMatrix * rotMatrix * transMatrix;
		DirectX::XMStoreFloat4x4(&world, worldMatrix);
	}
};

struct SpinComponent
{
	float angle = 0.0f;
	float speed = 1.0f;

	void Reflect(FieldList& f)
	{
		f.Add("Angle", angle);
		f.Add("Speed", speed);
	}
};

struct MeshComponent
{
	std::shared_ptr<Mesh> mesh;
	std::string FilePath;
	float scale = 1.0f;

	void Reflect(FieldList& f)
	{
		f.Add("FilePath", FilePath);
		f.AddRange("Scale", scale, 0.01f, 100.0f);

		std::string name = "Mesh";
		f.AddTexture(name, FilePath, mesh);
	}
};

struct SubMaterialRestore
{
	std::string materialAsset;	// .mat が割り当てられていれば、そのパス(値は .mat 側)
	std::string shaderName;
	float roughness = 0.5f;
	float metallic = 0.0f;
	float sssStrength = 0.0f;
	float sssWrap = 0.4f;
	float sssTrans = 0.0f;
	float sheen = 0.0f;
	COLOR sssColor = { 0.9f,0.35f,0.25f,1.0f };
	float baseAlpha = 1.0f;
	float reflectStrength = 0.0f;	// 平面反射。床のサブマテリアルだけ上げる
	float reflectFade = 8.0f;
	float reflectBlur = 1.0f;
	COLOR emissiveColor = { 0.0f,0.0f,0.0f,1.0f };
	float emissiveStrength = 1.0f;

	// 以前のシーンには保存されていない項目。キーが無いときはモデル由来の値を残す
	// (baseColor は PMX / glTF の拡散色が入っているので、既定値で上書きしてはいけない)
	std::optional<COLOR> baseColor;
	std::optional<COLOR> rimColor;
	std::optional<float> outlineWidth;
	std::optional<bool>  isFace;
};

struct MaterialComponent
{
	std::shared_ptr<Material> material;	// 単一
	std::vector<std::shared_ptr<Material>> materials;	// 複数
	std::vector<std::string> materialnames;	// 複数
	/// @brief サブマテリアルごとの .mat パス。空ならモデル内蔵のマテリアル
	/// @note materials と同じ添字。サイズが足りないときは空とみなす
	std::vector<std::string> materialAssets;
	ID3D12PipelineState* overridePso = nullptr;

	std::string FilePath;
	std::string RampFilePath;
	std::string shaderName = "Basic";
	std::vector<SubMaterialRestore> pendingSubs;

	void Reflect(FieldList& f)
	{
		f.Add("FilePath", FilePath);
		f.Add("RampFilePath", RampFilePath);
		f.Add("ShaderName", shaderName);
		f.AddTexture("Texture", FilePath, material);
	}
};

struct RigidBodyComponent
{
	PhysX::PxRigidActor* actor = nullptr;
	float mass = 1.0f;
	bool isKinematic = false;
	bool isStatic = false;
	bool useGravity = true;

	void Reflect(FieldList& f)
	{
		f.AddRange("Mass", mass, 0.01f, 1000.0f);
		f.Add("IsKinematic", isKinematic);
		f.Add("IsStatic", isStatic);
		f.Add("UseGravity", useGravity);
	}
};

struct ColliderComponent
{
	enum class ShapeType
	{
		Box,
		Sphere,
		Capsule,
		Mesh
	} shapeType = ShapeType::Box;
	SCALE size{ 1.0f, 1.0f, 1.0f };
	float radius = 0.5f;
	float friction = 0.5f;
	float restitution = 0.5f;
	float density = 1.0f;
	bool isTrigger = false;

	/// @brief 自分が属するレイヤー(0〜31)
	int layer = 0;

	/// @brief 衝突を許すレイヤーのビットマスク
	/// @note 既定は全許可。片方でも相手を弾いていれば衝突しない
	unsigned int collisionMask = 0xFFFFFFFFu;

	PhysX::PxShape* shape = nullptr;
	bool isShow = false;

	void Reflect(FieldList& f)
	{
		f.AddEnum("ShapeType", (int&)shapeType, { "Box", "Sphere", "Capsule", "Mesh" });
		f.Add("Size", size);
		f.AddRange("Radius", radius, 0.01f, 100.0f);
		f.AddRange("Friction", friction, 0.0f, 1.0f);
		f.AddRange("Restitution", restitution, 0.0f, 1.0f);
		f.AddRange("Density", density, 0.01f, 1000.0f);
		f.Add("IsTrigger", isTrigger);
		f.AddRange("Layer", layer, 0, 31);
		f.Add("IsShow", isShow);
	}

	/// @brief 相手と衝突してよいか
	bool CanCollideWith(const ColliderComponent& other) const
	{
		const unsigned int selfBit = 1u << (layer & 31);
		const unsigned int otherBit = 1u << (other.layer & 31);
		return (collisionMask & otherBit) != 0 && (other.collisionMask & selfBit) != 0;
	}
};

struct Ray
{
	float3 origin{ 0.0f,0.0f,0.0f };
	float3 direction{ 0.0f,0.0f,1.0f };	// 正規化された方向ベクトル

	/// @brief レイに沿って指定距離進んだ3D座標を取得
	float3 GetPoint(float distance)const
	{
		return float3{
			origin.x + direction.x * distance,
			origin.y + direction.y * distance,
			origin.z + direction.z * distance
		};
	}

	bool IntersectPanelY(float planeY, _Out_ float3& outHit)const
	{
		if (fabsf(direction.y) < 1e-4f) return false;
		const float t = (planeY - origin.y) / direction.y;
		if (t < 0.0f) return false;	// カメラの背後

		outHit = GetPoint(t);
		return true;
	}
};

struct CameraComponent
{
	/// @brief 描画タイプ
	enum class Projection
	{
		Perspective,
		Orthographic
	} projection = Projection::Perspective;

	enum class CameraType
	{
		Main,
		Secondary
	} cameraType = CameraType::Main;
	float orhoSize = 10.0f;
	float fovY = 60.0f;
	float nearZ = 0.1f;
	float farZ = 100.0f;
	bool isActive = true;

	float4x4 view{};
	float4x4 proj{};

	void Reflect(FieldList& f)
	{
		f.AddEnum("Projection", (int&)projection, { "Perspective", "Orthographic" });
		f.AddEnum("CameraType", (int&)cameraType, { "Main", "Secondary" });
		f.AddRange("OrthoSize", orhoSize, 0.01f, 100.0f);
		f.AddRange("FovY", fovY, 1.0f, 179.0f);
		f.AddRange("NearZ", nearZ, 0.01f, 100.0f);
		f.AddRange("FarZ", farZ, 1.0f, 10000.0f);
		f.Add("IsActive", isActive);
	}

	Ray ScreenPointToRay(float screenX,float screenY,float screenWidth,float screenHeight)const
	{
		using namespace DirectX;
		if (screenWidth <= 0.0f || screenHeight <= 0.0f)return Ray{};

		// UV座標
		const float u = screenX / screenWidth;
		const float v = screenY / screenHeight;

		// ビュー x 射影行列の逆行列
		const matrix viewM = XMLoadFloat4x4(&view);
		const matrix projM = XMLoadFloat4x4(&proj);
		XMVECTOR det;
		const matrix invVP = XMMatrixInverse(&det, XMMatrixMultiply(viewM, projM));
		if (XMVectorGetX(XMVectorEqual(det, XMVectorZero()))) return Ray{};

		// NDC 座標 (-1.0 ~ -1.0)で手前と奥を逆変換
		const float ndcX = u * 2.0f - 1.0f;
		const float ndcY = 1.0f - v * 2.0f;
		const XMVECTOR nearP = XMVector3TransformCoord(XMVectorSet(ndcX,ndcY,0.0f,1.0f),invVP);
		const XMVECTOR farP = XMVector3TransformCoord(XMVectorSet(ndcX,ndcY,1.0f,1.0f),invVP);

		Ray ray;
		XMStoreFloat3(&ray.origin, nearP);
		XMStoreFloat3(&ray.direction, XMVector3Normalize(XMVectorSubtract(farP,nearP)));
		return ray;
	}
};

struct CameraAnimationComponent
{
	std::string vmdPath;
	bool  playing = false;
	bool  loop = false;
	float time = 0.0f;

	CameraClip clip;      // 実行時データ(シリアライズ対象外)
	bool loaded = false;

	void Reflect(FieldList& f)
	{
		f.Add("VmdPath", vmdPath);
		f.Add("Playing", playing);
		f.Add("Loop", loop);
	}
};

struct FreeLookComponent
{
	float moveSpeed = 5.0f;
	float rotateSpeed = 0.0025f;
	float yaw = 0.0f;
	float pitch = 0.0f;
	bool Enabled = true;

	void Reflect(FieldList& f)
	{
		f.AddRange("MoveSpeed", moveSpeed, 0.01f, 100.0f);
		f.AddRange("RotateSpeed", rotateSpeed, 0.0001f, 1.0f);
		f.AddRange("Yaw", yaw, -360.0f, 360.0f);
		f.AddRange("Pitch", pitch, -89.0f, 89.0f);
		f.Add("Enabled", Enabled);
	}
};

// 被写界深度。カメラに付ける。合焦距離をタイムラインで動かせばフォーカス送りになる
struct DepthOfFieldComponent
{
	bool  enabled = true;
	float focusDistance = 6.0f;	// ピントの合う距離(カメラから)
	float focusRange = 1.0f;	// この幅は完全にシャープなまま
	float falloff = 8.0f;		// 合焦幅の外、この距離で最大ボケになる
	float maxBlur = 12.0f;		// 最大ボケ半径(半解像度でのピクセル)

	void Reflect(FieldList& f)
	{
		f.Add("Enabled", enabled);
		f.AddRange("FocusDistance", focusDistance, 0.1f, 60.0f);
		f.AddRange("FocusRange", focusRange, 0.0f, 20.0f);
		f.AddRange("Falloff", falloff, 0.1f, 40.0f);
		f.AddRange("MaxBlur", maxBlur, 0.0f, 40.0f);
	}
};

// このEntityに当たるライトを、影響の強い順に上位N灯へ絞る。
// キャラのように小さく、多灯が集中する対象向け。
// ステージのような巨大メッシュに付けると、遠い側の面が暗くなるので付けない
struct LightCullComponent
{
	int   maxLights = 12;
	float radius = 2.0f;	// この球に届かないライトは最初から捨てる

	void Reflect(FieldList& f)
	{
		f.AddRange("MaxLights", maxLights, 1, 64);
		f.AddRange("Radius", radius, 0.1f, 30.0f);
	}
};

// 床などの平面に映り込みを出す。有効なものを1つだけ使う
struct PlanarReflectionComponent
{
	bool  enabled = true;
	float planeY = 0.0f;			// 反射面の高さ(ステージ床のY)
	float resolutionScale = 0.5f;	// 反射RTをシーンRTの何倍で持つか

	void Reflect(FieldList& f)
	{
		f.Add("Enabled", enabled);
		f.AddRange("PlaneY", planeY, -20.0f, 20.0f);
		f.AddRange("ResolutionScale", resolutionScale, 0.25f, 1.0f);
	}
};

// このEntityを平面反射に映す(付いていないものは映らない)
struct ReflectionCasterComponent
{
	bool enabled = true;

	void Reflect(FieldList& f)
	{
		f.Add("Enabled", enabled);
	}
};

struct MusicSyncComponent
{
	float offset = 0.0f;        // モーションを曲に対して前後させる(秒、+で遅らせる)
	bool  syncAnimators = true; // 全AnimatorをこのAudioに同期
	bool  syncCamera = true;    // カメラVMDも同期
	float musicTime = 0.0f;
	// ランタイム状態(シリアライズ対象外)
	std::uint64_t startSamples = 0;
	bool started = false;
	float seekBase = 0.0f;			// シーク先の曲位置(秒)
	bool  resyncRequested = false;	// startSamplesを取り直す

	void Reflect(FieldList& f)
	{
		f.Add("Offset", offset);
		f.Add("SyncAnimators", syncAnimators);
		f.Add("SyncCamera", syncCamera);
	}
};

struct LightComponent
{
	enum class LightType
	{
		Directional,
		Point,
		Spot,
		Laser
	} type = LightType::Directional;
	COLOR    color{ 1.0f, 1.0f, 1.0f, 1.0f };
	float    intensity = 1.0f;
	float    range = 10.0f;
	POSITION direction{ 0.0f, -1.0f, 0.0f };
	float    spotAngle = 45.0f;
	bool     isActive = true;
	bool     castShadows = true;
	bool     showGizmo = false; // 旧 isShow (ギズモ・デバッグライン表示)
	void Reflect(FieldList& f)
	{
		f.AddEnum("Type", (int&)type, { "Directional", "Point", "Spot", "Laser" });
		f.Add("Color", color);
		f.AddRange("Intensity", intensity, 0.0f, 10.0f);
		f.AddRange("Range", range, 0.0f, 100.0f);
		f.Add("Direction", direction);
		f.AddRange("SpotAngle", spotAngle, 1.0f, 179.0f);
		f.Add("IsActive", isActive);
		f.Add("CastShadows", castShadows);
		f.Add("ShowGizmo", showGizmo);
	}
};

struct LightBeamComponent
{
	bool  showBeam = true;
	float beamWidth = 0.05f;
	COLOR beamColorEnd{ 1.0f, 1.0f, 1.0f, 1.0f };
	float glowIntensity = 3.0f;
	float volumetricIntensity = 1.0f;
	void Reflect(FieldList& f)
	{
		f.Add("ShowBeam", showBeam);
		f.AddRange("BeamWidth", beamWidth, 0.01f, 2.0f);
		f.Add("BeamColorEnd", beamColorEnd);
		f.AddRange("GlowIntensity", glowIntensity, 0.1f, 20.0f);
		f.AddRange("VolumetricIntensity", volumetricIntensity, 0.0f, 10.0f);
	}
};

struct NameComponent
{
	std::string name;
};


struct PrefabComponent
{
	std::string name;
	std::string guid;
};

struct SpriteComponent
{
	std::wstring texturePath;
	float2 size{ 1.0f, 1.0f };
	std::shared_ptr<Material> material;

	void Reflect(FieldList& f)
	{
		f.Add("TexturePath", texturePath);
		f.Add("Size", size);
	}
};


struct ScriptComponent
{
	std::vector<std::string> scriptNames;
	std::vector<std::unique_ptr<MonoBehavior>> behaviors;

	std::unordered_map<std::string, std::vector<FieldDesc>>                        fieldDescs; // 表示用メタ
	std::unordered_map<std::string, std::unordered_map<std::string, FieldValue>>   values;     // 値
};

struct CanvasComponent
{
	float2 ReferenceResolution{ 1920.0f, 1080.0f };	// 基準解像度
	int sortingOrder = 0;							// 描画順序

	void Reflect(FieldList& f)
	{
		f.Add("ReferenceResolution", ReferenceResolution);
		f.Add("SortingOrder", sortingOrder);
	}
};

struct RectTransformComponent
{
	float2 AnchoredPosition{ 0.0f, 0.0f };	// アンカー位置
	float2 SizeDelta{ 100.0f, 100.0f };
	float2 pivot{ 0.5f, 0.5f };				// ピボット位置

	Entity parent = INVALID_ENTITY;			// 親のEntity

	void Reflect(FieldList& f)
	{
		f.Add("AnchoredPosition", AnchoredPosition);
		f.Add("SizeDelta", SizeDelta);
		f.Add("Pivot", pivot);
	}
};

/// @brief クリックできるUI
/// @note RectTransform の矩形にマウスが入っているかで判定する。
///       onClick はスクリプトの OnStart で差し込む
struct UIButtonComponent
{
	bool interactable = true;

	COLOR normalColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	COLOR hoverColor{ 0.85f, 0.85f, 0.85f, 1.0f };
	COLOR pressedColor{ 0.6f, 0.6f, 0.6f, 1.0f };
	COLOR disabledColor{ 0.4f, 0.4f, 0.4f, 0.5f };

	// UIButtonSystem が更新する状態(シリアライズしない)
	bool isHovered = false;
	bool isPressed = false;

	/// @brief 押して離されたときに呼ばれる
	std::function<void()> onClick;

	void Reflect(FieldList& f)
	{
		f.Add("Interactable", interactable);
		f.Add("NormalColor", normalColor);
		f.Add("HoverColor", hoverColor);
		f.Add("PressedColor", pressedColor);
		f.Add("DisabledColor", disabledColor);
	}
};

/// @brief 種別の目印
/// @note 衝突相手の判定などに使う。名前と違って重複してよい
struct TagComponent
{
	std::string tag;

	void Reflect(FieldList& f)
	{
		f.Add("Tag", tag);
	}
};

struct UIImageComponent
{
	std::string texturePath;
	COLOR color{ 1.0f, 1.0f, 1.0f, 1.0f };
	std::shared_ptr<Material> material;

	/// @brief 描画するか。ポーズメニューの出し入れに使う
	bool visible = true;

	// UIButton が付いていれば、その状態色を掛けたものがここに入る(非シリアライズ)
	COLOR runtimeColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	// 実際にテクスチャへ書いた色。変化したときだけ塗り直すための記録
	COLOR uploadedColor{ -1.0f, -1.0f, -1.0f, -1.0f };

	void Reflect(FieldList& f)
	{
		f.Add("TexturePath", texturePath);
		f.Add("Color", color);
		f.Add("Visible", visible);

		f.AddTexture("Texture", texturePath, material);
	}
};

struct UITextComponent
{
	/// @brief 描画するか
	bool visible = true;
	std::string text = "Text";
	// Windowsフォント
	std::string fontPath = "C:\\Windows\\Fonts\\meiryo.ttc";
	float fontSize = 32.0f;
	COLOR color{ 1.0f, 1.0f, 1.0f, 1.0f };
	std::shared_ptr<Mesh> mesh;

	bool isDirty = true;
	std::string _lastText;

	void Reflect(FieldList& f)
	{
		f.Add("Visible", visible);
		f.Add("Text", text);
		f.Add("FontPath", fontPath);
		f.AddRange("FontSize", fontSize, 1.0f, 200.0f);
		f.Add("Color", color);

		f.AddFont("Font", fontPath);
	}
};

struct AudioSourceComponent
{
	std::string clipPath;       // 音声ファイル
	float volume = 1.0f;
	bool  loop = false;
	bool  playOnStart = false;  // Play開始時に自動再生

	// 3D設定
	bool is3D = true;
	float minDistance = 1.0f;
	float maxDistance = 100.0f;

	// ランタイム状態（シリアライズ対象外）
	IXAudio2SourceVoice* voice = nullptr;
	std::shared_ptr<AudioClip> clip;
	bool playRequested = false; // スクリプトから立てる
	bool stopRequested = false;

	bool  seekRequested = false;	// 指定秒へシーク
	float seekSeconds = 0.0f;
	bool  pauseRequested = false;	// 位置を保ったまま停止
	bool  resumeRequested = false;	// 停止位置から再開

	// いま鳴っているか(シリアライズ対象外)。
	// シークでバッファを投げ直すとき、止まっていたのに再生が始まらないよう見る
	bool  isPlaying = false;

	void Reflect(FieldList& f)
	{
		f.AddAudio("Clip", clipPath, clip);
		f.AddRange("Volume", volume, 0.0f, 1.0f);
		f.Add("Loop", loop);
		f.Add("PlayOnStart", playOnStart);
		f.Add("Is3D", is3D);
		f.AddRange("MinDistance", minDistance, 0.1f, 100.0f);
		f.AddRange("MaxDistance", maxDistance, 1.0f, 500.0f);
	}
};

struct AudioListenerComponent
{
	void Reflect(FieldList& f)
	{
		// AudioListenerには特にフィールドはない
	}
};

struct AnimatorComponent
{
	Skeleton skeleton;
	SkinData skinData;
	std::vector<AnimationClip> clips;
	int currentClip = 0;
	float time = 0.0f;
	bool playing = false;
	std::vector<float4x4> palette;
	std::vector<std::string> extraClipNames;	// 追加のアニメーション名

	float speed = 1.0f;			// 再生速度
	bool  loop = true;			// ループ再生
	bool  scrubbing = false;		// スライダー操作中(この間は物理を止める)
	bool  physicsResetRequest = false;	// 次フレームで物理を再同期する

	std::string clipPathsStr;
	bool clipsRestored = false;

	// 選択中クリップの名前。非同期ロードの完了順でclipsの並びが変わるため、
	// 添字ではなく名前を正として復元する
	std::string currentClipName;

	MorphSet morphs;					// もーフ定義
	std::vector<float> morphWeights;	// 各モーフの重み(0.0 ~ 1.0)
	std::vector<float3> morphoffsets;	// CPUでブレンド済みの頂点オフセット
	bool morphDirty = true;				// モーフの重みが変更されたかどうか
	uint64_t morphVA = 0;				// このフレームで転送済みのモーフバッファ(GPU仮想アドレス)
	uint64_t morphVAFrame = 0;			// morphVA を転送したフレーム(RenderContext::frameSerial)

	// 表情を再生するクリップ。-1 = currentClip と同じものを使う。
	// MMDでは体(FightingMyWay.vmd)と表情(face.vmd)が別ファイルのことがあるため、
	// ボーン用とは別のクリップを指定できるようにしている。
	int morphClip = -1;

	/// @brief 名前でクリップを切り替える
	/// @param restart 同じクリップでも最初から再生し直すか
	/// @return 見つかって切り替えたら true(名前が無ければ false)
	bool Play(const std::string& name, bool restart = false)
	{
		for (size_t i = 0; i < clips.size(); ++i)
		{
			if (clips[i].name != name) continue;

			// 同じものが流れている途中なら触らない(毎フレーム呼ばれても先頭に戻らない)
			if (!restart && currentClip == static_cast<int>(i) && playing) return true;

			currentClip = static_cast<int>(i);
			currentClipName = name;
			time = 0.0f;
			playing = true;
			return true;
		}
		return false;
	}

	/// @brief 名前の一部が一致する最初のクリップを再生する
	/// @note fbx のクリップ名は "Armature|Walk" のように前置きが付くことがある
	bool PlayLike(const std::string& part, bool restart = false)
	{
		for (const auto& c : clips)
		{
			if (c.name.find(part) != std::string::npos) return Play(c.name, restart);
		}
		return false;
	}

	/// @brief いま流れているクリップの名前(無ければ空)
	const std::string& CurrentClipName() const
	{
		static const std::string empty;
		if (currentClip < 0 || currentClip >= static_cast<int>(clips.size())) return empty;
		return clips[currentClip].name;
	}

	void Reflect(FieldList& f)
	{
		f.Add("Time", time);
		f.Add("Playing", playing);
		f.Add("ClipPaths", clipPathsStr);
		f.Add("CurrentClip", currentClip);			// 復元直後の暫定値
		f.Add("CurrentClipName", currentClipName);	// 正はこちら
		f.Add("Speed", speed);
		f.Add("Loop",loop);
		f.Add("MorphClip", morphClip);
	}
};

struct MmdPhysicsComponent
{
	std::shared_ptr<MmdPhysics> impl;
	void Reflect(FieldList& f) {}
};

/// @brief ボーンチェーンの疑似物理（髪・スカート等の揺れもの）
///        チェーンとコリジョンは配列なので、FieldList に載せられる
///        文字列 configStr へ畳んでシリアライズする。編集は
///        ComponentRegistry の DrawExtraUI 特殊化で行う。
struct KawaiiPhysicsComponent
{
	KawaiiPhysicsSettings settings;
	std::shared_ptr<KawaiiPhysics> impl;	// 実行時のソルバ（シリアライズ対象外）

	bool enabled = true;
	bool debugDraw = false;					// チェーンとコリジョンのワイヤ表示
	std::string configStr;					// chains/colliders のシリアライズ結果
	bool configRestored = false;			// ロード直後に一度だけ復元する

	// 自動生成の元データ（PMX の剛体。シリアライズ対象外、再生成用に保持）
	PmxPhysics sourcePhysics;

	void Reflect(FieldList& f)
	{
		f.Add("Enabled", enabled);
		f.Add("DebugDraw", debugDraw);
		f.Add("Gravity", settings.gravity);
		f.Add("Wind", settings.wind);
		f.AddRange("TimeScale", settings.timeScale, 0.0f, 2.0f);
		f.Add("PropagateToDescendants", settings.propagateToDescendants);
		f.Add("Config", configStr);
	}

	/// @brief UI で編集した内容を configStr へ書き戻し、ソルバに再構築を要求する
	void CommitConfig()
	{
		configStr = KawaiiSerialize(settings);
		if (impl) impl->MarkDirty();
	}
};

struct ParticleEmitterComponent
{
	enum class EmitShape : uint8_t
	{
		Cone,
		Sphere,
		Box
	}shape = EmitShape::Cone;

	bool emitting		= true;	// スクリプトから ON / OFF
	float emitRate		= 20.0f;	// 1秒あたりの発生数
	int maxParticles	= 200;

	float lifeTime = 1.5f;
	float lifeTimeVariance = 0.5f;	// ランダム幅

	float startSize = 0.03f;
	float endSize = 0.0f;

	COLOR startColor{ 1.0f, 1.0f, 1.0f, 0.6f };
	COLOR endColor{ 1.0f, 1.0f, 1.0f, 0.0f };

	float speed = 0.3f;				// ビーム方向への流れ速度
	float speedVariance = 0.15f;
	float drift = 0.05f;

	bool followLight = true;
	float3 gravity{ 0.0f,0.0f,0.0f };

	void Reflect(FieldList& f)
	{
		f.AddEnum("Shape", (int&)shape, { "Cone", "Sphere", "Box" });
		f.Add("Emitting", emitting);
		f.AddRange("EmitRate", emitRate, 0.0f, 500.0f);
		f.AddRange("MaxParticles", maxParticles, 1.0f, 2000.0f);
		f.AddRange("LifeTime", lifeTime, 0.1f, 10.0f);
		f.AddRange("LifeTimeVariance", lifeTimeVariance, 0.0f, 5.0f);
		f.AddRange("StartSize", startSize, 0.001f, 1.0f);
		f.AddRange("EndSize", endSize, 0.0f, 1.0f);
		f.Add("StartColor", startColor);
		f.Add("EndColor", endColor);
		f.AddRange("Speed", speed, 0.0f, 10.0f);
		f.AddRange("SpeedVariance", speedVariance, 0.0f, 5.0f);
		f.AddRange("Drift", drift, 0.0f, 1.0f);
		f.Add("FollowLight", followLight);
	}

	// --- ランタイム専用（シリアライズ不要）---
	struct Particle
	{
		float3 pos;
		float3 velocity;
		float age = 0.0f;
		float life = 1.0f;
	};
	std::vector<Particle> particles;   // プールとして使い回す(容量固定・再利用)
	float spawnAccumulator = 0.0f;
};

struct CharacterControllerComponent
{
	// ---- 設定(インスペクタ / 保存される) ---- //
	float height = 1.0f;
	float radius = 0.3f;
	float stepOffset = 0.3f;
	float slopeLimit = 45.0f;
	float gravityScale = 1.0f;
	int layer = 0;
	unsigned int collisionMask = 0xFFFFFFFFu;
	float3 center{ 0.0f,0.0f,0.0f };// カプセルの中心(Transformの位置からのずれ)

	// ---- 実行時の状態(これは保存しない) ---- //
	float3 velocity{ 0.0f,0.0f,0.0f };
	float3 pendingMove{ 0.0f,0.0f,0.0f };
	bool isGrounded = false;
	bool hitCeiling = false;

	void Move(const float3& delta)
	{
		pendingMove.x += delta.x;
		pendingMove.y += delta.y;
		pendingMove.z += delta.z;
	}

	bool Jump(float speed)
	{
		if (!isGrounded) return false;
		velocity.y = speed;
		isGrounded = false;
		return true;
	}

	void Reflect(FieldList& f)
	{
		f.AddRange("Height", height, 0.1f, 10.0f);
		f.AddRange("Radius", radius, 0.1f, 5.0f);
		f.AddRange("StepOffset", stepOffset, 0.0f, 2.0f);
		f.AddRange("SlopeLimit", slopeLimit, 0.0f, 90.0f);
		f.AddRange("GravityScale", gravityScale, 0.0f, 10.0f);
		f.AddRange("Layer", layer, 0, 31);
		f.Add("Center", center);
	}
};

struct FollowCameraComponent
{
	std::string targetTag	= "Player";	// 追いかける相手のタグ
	float distance			= 5.0f;		// 対象からの距離
	float height			= 1.5f;		// 見る高さ (対象Entityの足元からの高さ)
	float rotateSpeed		= 0.005f;	// マウス感度	
	float padRotateSpeed	= 2.5f;		// パッド感度(ラジアン/秒)。マウスとは単位が違う
	bool  padInvertY		= false;	// 右スティックの上下を入れ替える	
	float positionLag		= 12.0f;	// 位置追従の速さ(大きいほど追いつく
	float minPitch			= -30.0f;	// 見下ろし / 見上げの限界(度)
	float maxPitch			= 60.0f;
	bool enabled			= true;
	bool captureCursor		= true;		// 再生中にカーソルを中央へ固定して隠す(ゲーム起動時のみ)

	// ---- 実行時 ---- //
	float yaw = 0.0f;
	float pitch = 0.2f;

	void Reflect(FieldList& f)
	{
		f.Add("TargetTag", targetTag);
		f.AddRange("Distance", distance, 0.1f, 20.0f);
		f.AddRange("Height", height, 0.0f, 10.0f);
		f.AddRange("RotateSpeed", rotateSpeed, 0.0001f, 0.01f);
		f.AddRange("PadRotateSpeed", padRotateSpeed, 0.1f, 10.0f);
		f.Add("PadInvertY", padInvertY);
		f.AddRange("PositionLag", positionLag, 1.0f, 30.0f);
		f.AddRange("MinPitch", minPitch, -89.0f, 0.0f);
		f.AddRange("MaxPitch", maxPitch, 0.0f, 89.0f);
		f.Add("Enabled", enabled);
		f.Add("CaptureCursor", captureCursor);
	}
};

struct TerrainComponent
{
	std::string HeightMapPath;

	std::string dataPath;

	float Width = 100.0f;	// X 方向の大きさ(ワールド単位)
	float Depth = 100.0f;	// Z 方向の大きさ
	float Height = 20.0f;	// 白(255)のときの高さ

	int gridX = 128;		// 分割数・頂点数は(gridX + 1) * (gridZ + 1)
	int gridZ = 128;
	float uvTiling = 16.0f; // テクスチャの繰り返し回数

	// ---- 実行時 ---- //
	std::vector<float> Heights;	// 0 ~ 1の高さ
	unsigned int heightsVersion = 0;	// 高さが変わるたびに増える(物理の作り直しの判定に使う)
	size_t BuiltSettings = 0;	// 作った時の設定のハッシュ

	void Reflect(FieldList& f)
	{
		f.AddAssetPath("Heighthmap", HeightMapPath);
		f.AddAssetPath("TerrainData", dataPath);
		f.AddRange("Width", Width, 1.0f, 10000.0f);
		f.AddRange("Depth", Depth, 1.0f, 10000.0f);
		f.AddRange("Height", Height, 1.0f, 1000.0f);
		f.AddRange("GridX", gridX, 2, 512);
		f.AddRange("GridZ", gridZ, 2, 512);
		f.AddRange("UVTiling", uvTiling, 1.0f, 128.0f);
	}
};
