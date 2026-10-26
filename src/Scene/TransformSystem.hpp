/*****************************************************************//**
 * \file   TransformSystem.hpp
 * \brief  Transformからワールド行列を計算する
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "ECS/World.hpp"
#include "Transform.hpp"

namespace TE
{
	inline void TransformSystem(World& world)
	{
		world.Each<Transform, LocalToWorld>([](Entity, const Transform& transform, LocalToWorld& localToWorld) {
			localToWorld.value = Matrix4x4::TRS(transform.position, transform.rotation, transform.scale);
			});
	}
}
