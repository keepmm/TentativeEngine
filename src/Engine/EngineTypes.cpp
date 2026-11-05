/**
 * \file EngineTypes.cpp
 * \brief エンジンのコンポーネントを TypeRegistry に登録する
 */
#include "Engine/EngineTypes.hpp"

#include "Game/Spin.hpp"
#include "Reflection.hpp"
#include "Renderer/MeshRenderer.hpp"
#include "Scene/Camera.hpp"
#include "Scene/Name.hpp"
#include "Scene/OrbitCamera.hpp"
#include "Scene/Transform.hpp"

#include <cstddef>

using namespace TE;

void RegisterEngineTypes(TypeRegistry& registry) {
    // Add Component のメニューはこの順に並ぶ
    registry.Register<Name>("Name", { TE_FIELD(Name, value) });
    registry.Register<Transform>("Transform", {
        TE_FIELD(Transform, position),
        TE_FIELD(Transform, rotation),
        TE_FIELD(Transform, scale),
    });
    // TransformSystem の計算結果なので、編集させない（行列はフィールドの型にないので名前だけ）
    registry.Register<LocalToWorld>("LocalToWorld", {});
    registry.Register<MeshRenderer>("MeshRenderer", {
        // mesh は番号だけなので、今は出さない（アセットの参照を作ってから）
        TE_FIELD(MeshRenderer, color),
    });
    registry.Register<Camera>("Camera", {
        TE_FIELD(Camera, fieldOfView),
        TE_FIELD(Camera, nearClip),
        TE_FIELD(Camera, farClip),
        TE_FIELD(Camera, clearColor),
    });
    // view / home（注視点・角度・距離）は入れ子の構造体なので、今は手ざわりの係数だけ
    registry.Register<OrbitCamera>("OrbitCamera", {
        TE_FIELD(OrbitCamera, rotateSpeed),
        TE_FIELD(OrbitCamera, zoomRate),
        TE_FIELD(OrbitCamera, panSpeed),
        TE_FIELD(OrbitCamera, minDistance),
        TE_FIELD(OrbitCamera, maxDistance),
    });
    registry.Register<Spin>("Spin", {
        TE_FIELD(Spin, axis),
        TE_FIELD(Spin, degreesPerSecond),
    });
}
