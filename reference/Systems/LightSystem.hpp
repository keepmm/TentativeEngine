#pragma once

#include "../RenderContext.hpp"

class World;

/// @brief 対象の周囲に効くライトだけを、影響の強い順に上位 maxLights 灯へ詰め直す
/// @param src    シーン全体のライト
/// @param center 対象の中心(ワールド)
/// @param radius 対象を包む球の半径
/// @param maxLights 残す灯数
/// @param dst    詰め直した結果
/// @note 平行光は距離で切れないので常に残す。影を落とす灯の添字も詰め直しに追従させる
void BuildCulledLightCB(const LightCB& src, const float3& center,
	float radius, int maxLights, LightCB& dst);

class LightSystem
{
public:
	void Apply(World& world);

	inline const LightCB& GetLightData() const noexcept
	{
		return m_Data;
	}

private:
	LightCB m_Data{};
};