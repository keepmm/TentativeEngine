/**
 * \file RenderSystem.cpp
 * \brief World の中身を Renderer に渡す
 */
#include "Renderer/RenderSystem.hpp"

#include "Renderer/MeshRenderer.hpp"
#include "Renderer/Renderer.hpp"
#include "Scene/Camera.hpp"
#include "Scene/Transform.hpp"

namespace TE
{
	bool FindMainCamera(World& world, float aspect, CameraMatrices& out)
	{
		bool found = false;
		world.Each<Transform, Camera>([&](Entity, const Transform& tr, const Camera& camera) {
			if (found) { return; }
			found = true;
			out.view = Matrix4x4::View(tr.position, tr.rotation);
			out.projection = Matrix4x4::PerspectiveFovLH(
				camera.fieldOfView, aspect, camera.nearClip, camera.farClip);
			out.clear = camera.clearColor;
			});
		return found;
	}

	void RenderSystem(World& world, Renderer& renderer)
	{
		// 描くものを積むだけ。Renderer の中でメッシュごとにまとめて、インスタンシングで 1 回ずつ描く
		world.Each<LocalToWorld, MeshRenderer>([&](Entity, const LocalToWorld& localToWorld, const MeshRenderer& meshRenderer) {
			renderer.Draw(meshRenderer.mesh, localToWorld.value, meshRenderer.color);
			});
	}
}
