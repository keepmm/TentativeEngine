/**
 * \file OrbitCamera.hpp
 * \brief 注視点のまわりを回るカメラ（ModelViewer の Camera を ECS 用にしたもの）
 */
#pragma once

#include "ECS/World.hpp"
#include "Math/Bounds.hpp"
#include "Math/Math.hpp"
#include "Platform/Input.hpp"
#include "Scene/Transform.hpp"

namespace TE {

    // カメラの見え方。この 4 つが決まれば位置と向きが決まる
    struct OrbitView {
        Vector3 target;          // 注視点
        float distance = 7.0f;   // 注視点からカメラまで
        float yaw = 0.0f;        // 度。カメラが向いている方角（Y 軸まわり。0 で +Z を向く）
        float pitch = 30.0f;     // 度。正で見下ろす（X 軸まわり）
    };

    // 同じ Entity の Transform を毎フレーム上書きするので、Transform を直接いじっても戻される
    struct OrbitCamera {
        OrbitView view;
        OrbitView home;  // F キーで戻る見え方

        // 係数は ModelViewer と同じ手ざわりになる値
        float rotateSpeed = 0.3f;   // 度 / ピクセル（ModelViewer の 0.005 rad ≒ 0.29°）
        float zoomRate = 0.12f;     // 1 ノッチで距離が何割変わるか（ModelViewer は 120 × 0.001 = 0.12）
        float panSpeed = 0.001f;    // 1 ピクセルで動く量 ÷ 距離。遠いほど大きく動かす
        float minDistance = 0.1f;
        float maxDistance = 1000.0f;  // Camera::farClip と同じ。これより遠いと注視点が描かれなくなる
    };

    // bounds 全体が画面に収まる見え方を作る（ModelViewer の LookAt(center, radius * 2.5)）。
    // 半径 r の球を縦の画角 60° で写すには、距離 r / sin(30°) = 2r が要る。余白を足して 2.5 倍
    inline OrbitView FrameBounds(const Bounds& bounds, float yaw, float pitch, const OrbitCamera& limits) {
        OrbitView view;
        view.target = bounds.center;
        view.yaw = yaw;
        view.pitch = pitch;
        // ModelViewer は LookAt で距離を制限しておらず、大きなモデルで 1000 を超えていたので、ここで抑える
        view.distance = Mathf::Clamp(bounds.extents.Magnitude() * 2.5f, limits.minDistance, limits.maxDistance);
        return view;
    }

    // 1 フレームぶんの入力。OrbitCameraSystem（Entity 用）と EditorCamera（エディター用）で共有する
    struct OrbitInput {
        bool rotating = false;
        bool panning = false;
        bool reset = false;
        Vector2 delta;
        float wheel = 0.0f;
    };

    // 左 / 右ドラッグ: 回転、中ドラッグ: パン、ホイール: ズーム、F: home に戻す（R はエディターの拡縮ギズモに使う）
    inline OrbitInput ReadOrbitInput(const Input& input) {
        OrbitInput in;
        in.rotating = input.GetMouseButton(MouseButton::Left) || input.GetMouseButton(MouseButton::Right);
        in.panning = input.GetMouseButton(MouseButton::Middle);
        in.delta = input.GetMouseDelta();
        in.wheel = input.GetMouseWheel();
        in.reset = input.GetKeyDown('F');
        return in;
    }

    // view を入力で動かし、その結果を transform に書く。OrbitInput{}（何も押していない）を渡せば、位置と向きを計算し直すだけになる
    inline void UpdateOrbit(OrbitCamera& camera, Transform& transform, const OrbitInput& in) {
        OrbitView& view = camera.view;
        if (in.reset) {
            view = camera.home;
        }

        if (in.rotating) {
            // 右へドラッグで右へ回り込む。下へドラッグでカメラが上へ回り込む（ModelViewer と同じ向き）
            view.yaw = Mathf::Repeat(view.yaw + in.delta.x * camera.rotateSpeed, 360.0f);
            // 真上・真下を向くと「上」が決まらずカメラがひっくり返るので、手前で止める
            view.pitch = Mathf::Clamp(view.pitch + in.delta.y * camera.rotateSpeed, -89.0f, 89.0f);
        }

        // 回転を反映した後の向きで、パンの向きと位置を決める（先に作ると 1 フレーム遅れる）
        const Quaternion rotation = Quaternion::Euler(view.pitch, view.yaw, 0.0f);

        if (in.panning) {
            // 画面上でつかんだものがマウスに付いてくるよう、カメラはマウスと逆に動かす。
            // マウスの y は下が正なので、上方向の符号は x と逆になる
            const float scale = view.distance * camera.panSpeed;
            view.target += (rotation * Vector3::up) * (in.delta.y * scale) - (rotation * Vector3::right) * (in.delta.x * scale);
        }

        if (in.wheel != 0.0f) {
            // 掛け算で縮めると、近いときは少し、遠いときは大きく動く。
            // ModelViewer の「d -= d × 0.12」を、何ノッチ分でも距離が負にならない形（べき乗）にした
            view.distance = Mathf::Clamp(view.distance * Mathf::Pow(1.0f - camera.zoomRate, in.wheel),
                camera.minDistance, camera.maxDistance);
        }

        // カメラは注視点から「向いている方向の逆」へ distance だけ下がった所にいる
        transform.rotation = rotation;
        transform.position = view.target - (rotation * Vector3::forward) * view.distance;
    }

    // OrbitCamera を持つ Entity の Transform を毎フレーム上書きする
    inline void OrbitCameraSystem(World& world, const Input& input) {
        const OrbitInput in = ReadOrbitInput(input);
        world.Each<Transform, OrbitCamera>([&](Entity, Transform& transform, OrbitCamera& camera) {
            UpdateOrbit(camera, transform, in);
            });
    }

}  // namespace TE