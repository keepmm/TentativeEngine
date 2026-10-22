/*****************************************************************//**
 * \file   Math.hpp
 * \brief  数学ライブラリをまとめてIncludeする
 * 
 * \author keepm
 * \date   October 2026
 * 
 * 
 * 規約
 * 座標系 左手
 * 正の回転 軸の + 軸から原点を見て時計回り
 * 行列 m[行][列]、行ベクトル(v * M) 合成は左から順に聞く
 * 深度 投射行列はD3Dの範囲
 *********************************************************************/
#pragma once

#include "Math/Mathf.hpp"
#include "Math/Vector2.hpp"
#include "Math/Vector3.hpp"
#include "Math/Vector4.hpp"
#include "Math/VectorInt.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Matrix4x4.hpp"
#include "Math/Color.hpp"
#include "Math/Rect.hpp"
#include "Math/Ray.hpp"
#include "Math/Plane.hpp"
#include "Math/Bounds.hpp"
#include "Math/BoundingSphere.hpp"
#include "Math/Frustum.hpp"
#include "Math/Intersection.hpp"
#include "Math/Random.hpp"
#include "Math/Easing.hpp"
#include "Math/Half.hpp"