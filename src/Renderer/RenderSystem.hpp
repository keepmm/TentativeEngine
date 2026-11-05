/**
 * \file RenderSystem.hpp
 * \brief World の中身を Renderer に渡す
 */
#pragma once

#include "ECS/World.hpp"
#include "Math/Color.hpp"
#include "Math/Matrix4x4.hpp"

class Renderer;

namespace TE {

	// Renderer::SetViewCamera に渡す 1 台ぶんのカメラ
	struct CameraMatrices {
		Matrix4x4 view;
		Matrix4x4 projection;
		Color clear;
	};

	// Camera を持つ最初の Entity から、ビュー行列・射影行列・背景色を作る。aspect = 描く先の幅 / 高さ。
	// カメラが 1 台もなければ false（out は変えない）。
	// Scene ビューと Game ビューが、それぞれの RT の縦横比で呼ぶ
	bool FindMainCamera(World& world, float aspect, CameraMatrices& out);

	// 描くもの（LocalToWorld + MeshRenderer）を集めて Renderer に積む。カメラは渡さない
	// （どのビューをどのカメラで描くかは、描く先を知っている側が SetViewCamera で決める）。
	// GPU への命令は出さない（それは Renderer::Render の仕事）。TransformSystem の後に呼ぶ
	void RenderSystem(World& world, Renderer& renderer);

}  // namespace TE
