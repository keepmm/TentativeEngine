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
	void RenderSystem(World& world, Renderer& renderer)
	{
		bool hasCamera = false;
		world.Each<Transform, Camera>([&](Entity, const Transform& tr, const Camera& camera) {
			if (hasCamera) { return; }
			hasCamera = true;
			const Matrix4x4 view = Matrix4x4::View(tr.position, tr.rotation);
			const Matrix4x4 projection = Matrix4x4::PerspectiveFovLH(
				camera.fieldOfView, renderer.AspectRatio(), camera.nearClip, camera.farClip);
			renderer.SetCamera(view, projection);
			renderer.SetClearColor(camera.clearColor);
			});

		// 描くものを積むだけ。Renderer の中でメッシュごとにまとめて、インスタンシングで 1 回ずつ描く
		world.Each<LocalToWorld, MeshRenderer>([&](Entity, const LocalToWorld& localToWorld, const MeshRenderer& meshRenderer) {
			renderer.Draw(meshRenderer.mesh, localToWorld.value, meshRenderer.color);
			});
	}
}

