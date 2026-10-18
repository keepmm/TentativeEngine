#include "LightSystem.hpp"
#include "../World.hpp"
#include "../Components.hpp"
#include "../DirectX.hpp"
#include "../Time.hpp"
#include <algorithm>
#include <cmath>

void BuildCulledLightCB(const LightCB& src, const float3& center,
	float radius, int maxLights, LightCB& dst)
{
	dst = src;

	const int count = static_cast<int>(src.lightCount.x);
	const int shadowIndex = static_cast<int>(src.shadowParams.w);

	// (スコア, 元の添字)。スコアは「その灯がこの対象をどれだけ照らすか」の目安
	struct Scored { float score; int index; };
	Scored scored[MAX_LIGHTS];
	int n = 0;

	for (int i = 0; i < count && i < static_cast<int>(MAX_LIGHTS); ++i)
	{
		const LightData& l = src.lights[i];
		const float lum = l.color.x * 0.299f + l.color.y * 0.587f + l.color.z * 0.114f;
		if (lum <= 0.0f) continue;

		// 平行光は距離減衰が無いので必ず残す(スコアを最大にする)
		if (static_cast<int>(l.param.x) == 0)
		{
			scored[n++] = { FLT_MAX, i };
			continue;
		}

		const float dx = l.posRange.x - center.x;
		const float dy = l.posRange.y - center.y;
		const float dz = l.posRange.z - center.z;
		const float dist = sqrtf(dx * dx + dy * dy + dz * dz);

		const float range = (std::max)(l.posRange.w, 0.0001f);
		if (dist - radius > range) continue;		// 球に届かない

		// 減衰はシェーダーと同じ形(1 - d/range)^2 を対象の一番近い点で見る
		const float d = (std::max)(dist - radius, 0.0f);
		const float atten = (1.0f - d / range) * (1.0f - d / range);
		scored[n++] = { lum * atten, i };
	}

	const int keep = (std::min)(n, (std::max)(1, maxLights));

	// 上位 keep 灯だけ前に寄せる(全体を並べ替える必要はない)
	std::partial_sort(scored, scored + keep, scored + n,
		[](const Scored& a, const Scored& b) { return a.score > b.score; });

	int newShadow = -1;
	for (int i = 0; i < keep; ++i)
	{
		dst.lights[i] = src.lights[scored[i].index];
		if (scored[i].index == shadowIndex) newShadow = i;
	}

	dst.lightCount.x = static_cast<float>(keep);

	// 影を落とす灯が落選したら、影そのものを切る(別の灯に影が付くと破綻する)
	if (newShadow < 0) dst.shadowParams.y = 0.0f;
	dst.shadowParams.w = static_cast<float>(newShadow);
}

void LightSystem::Apply(World& world)
{
	// ライトがない場合のためにデフォルト
	m_Data = {};

	UINT count = 0;

	// 環境光をシーンの灯りの色へ寄せるための集計
	float3 tintSum{ 0.0f, 0.0f, 0.0f };
	float  tintWeight = 0.0f;
	float  ambientBlend = 0.0f;

	// 影を落とすライト(先着1つ)。方向ライトでもスポットでもよい
	int   shadowIndex = -1;
	LightComponent::LightType shadowType = LightComponent::LightType::Directional;
	float shadowAngle = 45.0f;
	float shadowRange = 10.0f;
	float3 shadowDir{};
	float3 shadowPos{};

	world.Each<LightComponent>([&](Entity entity, LightComponent& light)
		{
			// 非アクティブ、または上限に達したらスキップ
			if (!light.isActive || count >= MAX_LIGHTS)
			{
				return;
			}

			LightData& dst = m_Data.lights[count];

			// 色 × 強度
			dst.color = light.color;
			dst.color.x *= light.intensity;
			dst.color.y *= light.intensity;
			dst.color.z *= light.intensity;

			// 位置と方向（Transformがあれば回転から導出）
			if (world.HasComponent<TransformComponent>(entity))
			{
				const auto& tr = world.GetComponent<TransformComponent>(entity);
				dst.posRange = float4(
					tr.position.x, tr.position.y, tr.position.z,
					light.range);

				const auto rot = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&tr.rotation));
				auto fwd = DirectX::XMVector3Rotate(
					DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rot);

				fwd = DirectX::XMVector3Normalize(fwd);

				// 不正な向き(NaN/Inf/ゼロ)はシェーダ側でNaNになり画面が黒く落ちるので弾く
				{
					float3 chk;
					DirectX::XMStoreFloat3(&chk, fwd);
					if (!std::isfinite(chk.x) || !std::isfinite(chk.y) || !std::isfinite(chk.z) ||
						(chk.x == 0.0f && chk.y == 0.0f && chk.z == 0.0f))
					{
						fwd = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
					}
				}

				DirectX::XMStoreFloat4(&dst.dir, fwd);

				// ギズモ表示用に書き戻す
				DirectX::XMStoreFloat3(&light.direction, fwd);
			}
			else
			{
				const auto dirVec = DirectX::XMVector3Normalize(DirectX::XMVectorSet(
					light.direction.x, light.direction.y, light.direction.z, 0.0f));
				DirectX::XMStoreFloat4(&dst.dir, dirVec);
				dst.posRange.w = light.range;
			}

			// タイプとスポット角
			dst.param.x = static_cast<float>(light.type);
			dst.param.y = cosf(DirectX::XMConvertToRadians(light.spotAngle * 0.5f));
			float beamWidth = 0.05f;
			float volumetricIntensity = 1.0f;
			if (world.HasComponent<LightBeamComponent>(entity))
			{
				const auto& beam = world.GetComponent<LightBeamComponent>(entity);
				beamWidth = beam.beamWidth;
				volumetricIntensity = beam.volumetricIntensity;
			}
			dst.param.z = beamWidth;
			dst.param.w = volumetricIntensity;

			// 環境光の色付け用に、点いている灯りの色を明るさで重み付けして集める
			{
				const float w = dst.color.x * 0.299f + dst.color.y * 0.587f
					+ dst.color.z * 0.114f;
				if (w > 0.0f)
				{
					tintSum.x += dst.color.x * w;
					tintSum.y += dst.color.y * w;
					tintSum.z += dst.color.z * w;
					tintWeight += w;
				}
			}

			// 環境光は最初のライトのものを採用
			{
				const auto& rs = RenderSettings::Get();
				ambientBlend = std::clamp(rs.ambientFromLights, 0.0f, 1.0f);
				if (APP->HasEnvironment())
				{
					const float3 e = APP->GetEnvAmbient();
					const float k = 0.5f;   // 環境光の強さ（好みで調整）
					m_Data.ambientColor = float4(e.x * k, e.y * k, e.z * k, 1.0f);
				}
				else
				{
					m_Data.ambientColor = rs.ambientColor;
				}
			}

			// 影の担当を決める。CastShadows を切れば次のライトへ回る
			if (shadowIndex < 0 && light.castShadows &&
				(light.type == LightComponent::LightType::Directional ||
					light.type == LightComponent::LightType::Spot))
			{
				shadowIndex = static_cast<int>(count);
				shadowType = light.type;
				shadowAngle = light.spotAngle;
				shadowRange = light.range;
				shadowPos = float3{ dst.posRange.x, dst.posRange.y, dst.posRange.z };
				shadowDir = float3{ dst.dir.x, dst.dir.y, dst.dir.z };
			}

			++count;
		});

	// ----------------------------- //
	//		シャドウマッピング	     //
	// ----------------------------- //
	m_Data.shadowParams = { 0.005f, 0.0f, 2048.0f, 0.0f };
	const bool found = (shadowIndex >= 0);

	if (found)
	{
		// 方向がゼロ/不正ならデフォルトへ（NaN行列防止）
		DirectX::XMVECTOR d = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&shadowDir));
		const float dist = 50.0f;
		const float ortho = 30.0f;
		const float mapSize = m_Data.shadowParams.z;   // 2048

		// --- カメラ追従：エディタ(FreeLook)カメラ優先、無ければ通常カメラ ---
		float3 camPos{ 0,0,0 }, camFwd{ 0,0,1 };
		bool got = false, gotEditor = false;
		world.Each<TransformComponent, CameraComponent>(
			[&](Entity e, TransformComponent& tr, CameraComponent&)
			{
				if (gotEditor) return;
				const bool editor = world.HasComponent<FreeLookComponent>(e);
				if (!got || editor)
				{
					camPos = tr.position;
					auto q = DirectX::XMLoadFloat4(&tr.rotation);
					auto fwd = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);
					DirectX::XMStoreFloat3(&camFwd, fwd);
					got = true; gotEditor = editor;
				}
			});

		// カメラの少し前方を影の中心に（Yは0=地面基準）
		float3 focus = {
			camPos.x + camFwd.x * (ortho * 0.4f),
			0.0f,
			camPos.z + camFwd.z * (ortho * 0.4f)
		};

		DirectX::XMVECTOR up = (fabsf(shadowDir.y) > 0.99f)
			? DirectX::XMVectorSet(1, 0, 0, 0) : DirectX::XMVectorSet(0, 1, 0, 0);

		DirectX::XMVECTOR center = DirectX::XMLoadFloat3(&focus);

		{
			const float texelWorld = ortho / mapSize;
			const DirectX::XMMATRIX lightRot =
				DirectX::XMMatrixLookAtLH(DirectX::XMVectorZero(), d, up);

			DirectX::XMVECTOR c = DirectX::XMVector3TransformCoord(center, lightRot);
			c = DirectX::XMVectorSet(
				floorf(DirectX::XMVectorGetX(c) / texelWorld) * texelWorld,
				floorf(DirectX::XMVectorGetY(c) / texelWorld) * texelWorld,
				DirectX::XMVectorGetZ(c),
				0.0f);

			DirectX::XMVECTOR det;
			const DirectX::XMMATRIX inv = DirectX::XMMatrixInverse(&det, lightRot);
			center = DirectX::XMVector3TransformCoord(c, inv);
		}

		DirectX::XMVECTOR lightPos = DirectX::XMVectorSubtract(center, DirectX::XMVectorScale(d, dist));

		DirectX::XMMATRIX lightView;
		DirectX::XMMATRIX lightProj;

		if (shadowType == LightComponent::LightType::Spot)
		{
			const float far_ = (std::max)(shadowRange, 2.0f);
			const DirectX::XMVECTOR sp = DirectX::XMLoadFloat3(&shadowPos);
			const DirectX::XMVECTOR target =
				DirectX::XMVectorAdd(sp, DirectX::XMVectorScale(d, far_));

			lightView = DirectX::XMMatrixLookAtLH(sp, target, up);

			const float fov = DirectX::XMConvertToRadians(
				(std::min)((std::max)(shadowAngle, 5.0f), 170.0f));
			lightProj = DirectX::XMMatrixPerspectiveFovLH(fov, 1.0f, 0.5f, far_);
		}
		else
		{
			lightView = DirectX::XMMatrixLookAtLH(lightPos, center, up);
			lightProj = DirectX::XMMatrixOrthographicLH(ortho, ortho, 1.0f, dist * 2.0f);
		}

		DirectX::XMStoreFloat4x4(&m_Data.lightviewproj,
			DirectX::XMMatrixTranspose(lightView * lightProj));

		m_Data.shadowParams.y = 1.0f;                          // 影を有効化
		m_Data.shadowParams.w = static_cast<float>(shadowIndex); // どのライトが落とすか
	}

	// --- 環境光をそのときの灯りの色へ寄せる ---
	if (tintWeight > 0.0f && ambientBlend > 0.0f)
	{
		float3 tint{ tintSum.x / tintWeight, tintSum.y / tintWeight,
					 tintSum.z / tintWeight };

		const float tintLuma = tint.x * 0.299f + tint.y * 0.587f + tint.z * 0.114f;
		if (tintLuma > 1e-4f)
		{
			tint.x /= tintLuma; tint.y /= tintLuma; tint.z /= tintLuma;

			auto& a = m_Data.ambientColor;
			a.x = std::lerp(a.x, a.x * tint.x, ambientBlend);
			a.y = std::lerp(a.y, a.y * tint.y, ambientBlend);
			a.z = std::lerp(a.z, a.z * tint.z, ambientBlend);
		}
	}

	m_Data.lightCount.x = static_cast<float>(count);
	m_Data.lightCount.y = RenderSettings::Get().envIntensity;   // IBL の強さ(シェーダー側で共有)
}
