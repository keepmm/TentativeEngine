/*****************************************************************//**
 * \file   Vertex.hpp
 * \brief  頂点データ構造体
 * 
 * 作成者 
 * 作成日 2026/2/12
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <DirectXMath.h>
#include "Defines.hpp"

struct Vertex
{
	float3 position;
	float3 normal;
	float4 col;
	float2 uv;
	float3 tangent;
	uint32_t boneIndices[4] = { 0,0,0,0 };
	float boneWeights[4] = { 0.0f,0.0f,0.0f,0.0f };
};
