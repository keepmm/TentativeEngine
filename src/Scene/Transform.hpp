/*****************************************************************//**
 * \file   Transform.hpp
 * \brief  位置・回転・拡大のコンポーネント
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "Math/Vector3.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Matrix4x4.hpp"

namespace TE
{
	struct Transform
	{
		Vector3 position;
		Quaternion rotation;
		Vector3 scale = Vector3::one;

		Vector3 Forward() const { return rotation * Vector3::forward; }
		Vector3 Right() const { return rotation * Vector3::right; }
		Vector3 Up() const { return rotation * Vector3::up; }
	};

	struct LocalToWorld
	{
		Matrix4x4 value;
	};
}
