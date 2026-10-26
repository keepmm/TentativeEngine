
/**
 * \file Spin.hpp
 * \brief その場で回り続けるコンポーネントと System（動作確認用のサンプル）
 */
#pragma once

#include "ECS/World.hpp"
#include "Scene/Transform.hpp"

namespace TE {

    struct Spin {
        Vector3 axis = Vector3::up;
        float degreesPerSecond = 45.0f;
    };

    inline void SpinSystem(World& world, float deltaSeconds) {
        world.Each<Transform, Spin>([deltaSeconds](Entity, Transform& transform, const Spin& spin) {
            // 今の向きに、さらに少し回す（ワールドの軸まわり）。掛け算を重ねると誤差で長さがずれるので正規化する
            transform.rotation = (Quaternion::AngleAxis(spin.degreesPerSecond * deltaSeconds, spin.axis) * transform.rotation).Normalized();
            });
    }

}  // namespace TE