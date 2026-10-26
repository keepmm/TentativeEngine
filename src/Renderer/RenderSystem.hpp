/**
 * \file RenderSystem.hpp
 * \brief World の中身を Renderer に渡す
 */
#pragma once

#include "ECS/World.hpp"

class Renderer;

namespace TE {

	// カメラ（Transform + Camera）と、描くもの（LocalToWorld + MeshRenderer）を集めて Renderer に渡す。
	// GPU への命令は出さない（それは Renderer::Render の仕事）。TransformSystem の後に呼ぶ
	void RenderSystem(World& world, Renderer& renderer);

}  // namespace TE