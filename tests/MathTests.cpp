/**
 * \file MathTests.cpp
 * \brief 数学ライブラリが Unity と同じ向き・同じ値になるかの確認
 */
#include "Math/Math.hpp"

#include <cstdio>

using namespace TE;

namespace {

int g_Failed = 0;

bool Near(const Vector3& a, const Vector3& b) {
    return Mathf::Approximately(a.x, b.x) && Mathf::Approximately(a.y, b.y) && Mathf::Approximately(a.z, b.z);
}

bool Near(const Color& a, const Color& b) {
    return Mathf::Abs(a.r - b.r) < 1e-4f && Mathf::Abs(a.g - b.g) < 1e-4f
        && Mathf::Abs(a.b - b.b) < 1e-4f && Mathf::Abs(a.a - b.a) < 1e-4f;
}

bool NearIdentity(const Matrix4x4& m) {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (Mathf::Abs(m.m[row][col] - (row == col ? 1.0f : 0.0f)) > 1e-4f) {
                return false;
            }
        }
    }
    return true;
}

// q と -q は同じ回転なので、成分ではなく「回した結果」で比べる
bool SameRotation(const Quaternion& a, const Quaternion& b) {
    return Quaternion::Angle(a, b) < 0.01f;
}

}  // namespace

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_Failed;                                                     \
        }                                                                   \
    } while (0)

static void TestMathf() {
    CHECK(Mathf::Round(2.5f) == 2.0f && Mathf::Round(3.5f) == 4.0f);  // 偶数への丸め
    CHECK(Mathf::Approximately(Mathf::SmoothStep(0.0f, 10.0f, 0.5f), 5.0f));
    CHECK(Mathf::Approximately(Mathf::PingPong(3.0f, 2.0f), 1.0f));
    CHECK(Mathf::Approximately(Mathf::DeltaAngle(350.0f, 10.0f), 20.0f));
    CHECK(Mathf::Approximately(Mathf::LerpAngle(350.0f, 10.0f, 0.5f), 360.0f));  // 0 度をまたいで近い方
    CHECK(Mathf::Approximately(Mathf::MoveTowardsAngle(350.0f, 10.0f, 5.0f), 355.0f));
    CHECK(Mathf::IsPowerOfTwo(256) && !Mathf::IsPowerOfTwo(255) && !Mathf::IsPowerOfTwo(0));
    CHECK(Mathf::NextPowerOfTwo(300) == 512 && Mathf::NextPowerOfTwo(256) == 256 && Mathf::NextPowerOfTwo(1) == 1);

    float velocity = 0.0f;
    float position = 0.0f;
    for (int frame = 0; frame < 300; ++frame) {
        position = Mathf::SmoothDamp(position, 10.0f, velocity, 0.3f, 1.0f / 60.0f);
    }
    CHECK(Mathf::Approximately(position, 10.0f));
}

static void TestVectors() {
    CHECK(Near(Vector3::Cross(Vector3::right, Vector3::up), Vector3::forward));  // 左手系
    CHECK(Mathf::Approximately(Vector3::Angle(Vector3::right, Vector3::forward), 90.0f));
    CHECK(Mathf::Approximately(Vector3::SignedAngle(Vector3::forward, Vector3::right, Vector3::up), 90.0f));
    CHECK(Mathf::Approximately(Vector3::SignedAngle(Vector3::forward, Vector3::left, Vector3::up), -90.0f));
    CHECK(Near(Vector3::MoveTowards(Vector3::zero, { 10.0f, 0.0f, 0.0f }, 3.0f), { 3.0f, 0.0f, 0.0f }));
    CHECK(Near(Vector3::Reflect({ 1.0f, -1.0f, 0.0f }, Vector3::up), { 1.0f, 1.0f, 0.0f }));
    CHECK(Near(Vector3::ProjectOnPlane({ 1.0f, 5.0f, 2.0f }, Vector3::up), { 1.0f, 0.0f, 2.0f }));

    Vector3 indexed{ 1.0f, 2.0f, 3.0f };
    indexed[2] = 9.0f;
    CHECK(indexed[0] == 1.0f && indexed[1] == 2.0f && indexed.z == 9.0f);

    CHECK(Mathf::Approximately(Vector2::SignedAngle(Vector2::right, Vector2::up), 90.0f));  // 2D は反時計回りが正
    const Vector2 perpendicular = Vector2::Perpendicular(Vector2::right);
    CHECK(Mathf::Approximately(perpendicular.x, 0.0f) && Mathf::Approximately(perpendicular.y, 1.0f));

    CHECK(Near((Vector4::Point(Vector3::one) * 2.0f).XYZ(), { 2.0f, 2.0f, 2.0f }));
    CHECK(Vector2Int::RoundToInt({ 1.4f, 2.6f }) == (Vector2Int{ 1, 3 }));
    CHECK(Vector3Int::FloorToInt({ -0.5f, 1.5f, 2.0f }) == (Vector3Int{ -1, 1, 2 }));
}

static void TestQuaternion() {
    // Unity と同じ向きになる
    CHECK(Near(Quaternion::AngleAxis(90.0f, Vector3::up) * Vector3::right, Vector3::back));
    CHECK(Near(Quaternion::Euler(0.0f, 90.0f, 0.0f) * Vector3::forward, Vector3::right));  // yaw +90 で右を向く
    CHECK(Near(Quaternion::Euler(90.0f, 0.0f, 0.0f) * Vector3::forward, Vector3::down));   // pitch +90 で下を向く
    CHECK(Near(Quaternion::Euler(0.0f, 0.0f, 90.0f) * Vector3::right, Vector3::up));       // roll +90 で右が上へ

    // 掛け算は右から効く: (a * b) * v == a * (b * v)
    const Quaternion a = Quaternion::Euler(30.0f, 0.0f, 0.0f);
    const Quaternion b = Quaternion::Euler(0.0f, 0.0f, 60.0f);
    const Vector3 v{ 0.2f, -0.7f, 1.5f };
    CHECK(Near((a * b) * v, a * (b * v)));

    // Euler は Z → X → Y の順
    const Quaternion euler = Quaternion::Euler(30.0f, 45.0f, 60.0f);
    CHECK(SameRotation(euler, Quaternion::Euler(0.0f, 45.0f, 0.0f) * Quaternion::Euler(30.0f, 0.0f, 0.0f)
                                  * Quaternion::Euler(0.0f, 0.0f, 60.0f)));

    // EulerAngles で元の角度に戻る
    CHECK(Near(euler.EulerAngles(), { 30.0f, 45.0f, 60.0f }));
    CHECK(Near(Quaternion::Euler(-30.0f, 0.0f, 0.0f).EulerAngles(), { 330.0f, 0.0f, 0.0f }));  // 0〜360 で返る
    // ジンバルロックの近くでも、同じ回転には戻る
    const float pitches[] = { 90.0f, -90.0f, 89.99f, 89.9f, 89.0f };
    for (const float pitch : pitches) {
        const Quaternion nearLocked = Quaternion::Euler(pitch, 30.0f, 20.0f);
        CHECK(SameRotation(Quaternion::Euler(nearLocked.EulerAngles()), nearLocked));
    }

    CHECK(SameRotation(Quaternion::Inverse(euler) * euler, Quaternion::identity));

    // LookRotation: 前が forward、右は水平のまま
    const Vector3 look = Vector3{ 1.0f, 1.0f, 2.0f }.Normalized();
    const Quaternion lookRotation = Quaternion::LookRotation(look);
    CHECK(Near(lookRotation * Vector3::forward, look));
    CHECK(Mathf::Approximately((lookRotation * Vector3::right).y, 0.0f));
    CHECK(Near(Quaternion::LookRotation(Vector3::down) * Vector3::forward, Vector3::down));  // 上と平行

    CHECK(Near(Quaternion::FromToRotation(Vector3::right, Vector3::forward) * Vector3::right, Vector3::forward));
    CHECK(Near(Quaternion::FromToRotation(Vector3::right, Vector3::left) * Vector3::right, Vector3::left));  // 真逆

    // Slerp の中間は半分の角度
    const Quaternion half = Quaternion::Slerp(Quaternion::identity, Quaternion::AngleAxis(90.0f, Vector3::up), 0.5f);
    CHECK(SameRotation(half, Quaternion::AngleAxis(45.0f, Vector3::up)));
    const Quaternion step = Quaternion::RotateTowards(Quaternion::identity, Quaternion::AngleAxis(90.0f, Vector3::up), 10.0f);
    // Slerp の中で acos を使うので、角度には 0.0001 度ほどの誤差が乗る
    CHECK(Mathf::Abs(Quaternion::Angle(Quaternion::identity, step) - 10.0f) < 0.001f);
    CHECK(Mathf::Abs(Quaternion::Angle(Quaternion::identity, Quaternion::AngleAxis(0.5f, Vector3::up)) - 0.5f) < 0.0001f);
}

static void TestMatrix() {
    const Quaternion a = Quaternion::Euler(30.0f, 0.0f, 0.0f);
    const Quaternion b = Quaternion::Euler(0.0f, 0.0f, 60.0f);
    const Vector3 v{ 0.2f, -0.7f, 1.5f };

    // 行列は左から順に効くので、クォータニオンとは並びが逆になる
    CHECK(Near(Matrix4x4::Rotate(a * b).MultiplyVector(v), (Matrix4x4::Rotate(b) * Matrix4x4::Rotate(a)).MultiplyVector(v)));

    // 拡大 → 回転 → 移動
    const Matrix4x4 world = Matrix4x4::TRS({ 10.0f, 0.0f, 0.0f }, Quaternion::Euler(0.0f, 90.0f, 0.0f), { 2.0f, 2.0f, 2.0f });
    CHECK(Near(world.MultiplyPoint3x4(Vector3::forward), { 12.0f, 0.0f, 0.0f }));
    CHECK(Near(world.GetPosition(), { 10.0f, 0.0f, 0.0f }));

    // 逆行列を掛けると単位行列に戻る
    const Matrix4x4 skewed = Matrix4x4::TRS({ 1.0f, -2.0f, 3.0f }, Quaternion::Euler(10.0f, 20.0f, 30.0f), { 2.0f, 0.5f, 3.0f });
    CHECK(NearIdentity(skewed * skewed.Inverse()));
    CHECK(Mathf::Approximately(skewed.Determinant(), 3.0f));  // 行列式 = 体積の倍率 = 2 * 0.5 * 3
    CHECK(Matrix4x4::Scale({ 1.0f, 0.0f, 1.0f }).Inverse().m[0][0] == 0.0f);  // つぶれた行列は zero

    // 法線行列: 軸ごとに拡大率が違っても、面に垂直なまま
    const Matrix4x4 stretch = Matrix4x4::Scale({ 4.0f, 1.0f, 1.0f });
    const Vector3 surfaceNormal = Vector3{ 1.0f, 1.0f, 0.0f }.Normalized();  // 面 x + y = 0 の法線
    const Vector3 transformedTangent = stretch.MultiplyVector({ 1.0f, -1.0f, 0.0f });  // 面に沿った向き
    CHECK(Mathf::Approximately(Vector3::Dot(stretch.NormalMatrix().MultiplyVector(surfaceNormal), transformedTangent), 0.0f));

    // ビュー行列はカメラのワールド行列を打ち消す
    const Vector3 look = Vector3{ 1.0f, 1.0f, 2.0f }.Normalized();
    const Vector3 cameraPosition{ 3.0f, 2.0f, -5.0f };
    const Matrix4x4 view = Matrix4x4::View(cameraPosition, Quaternion::LookRotation(look));
    CHECK(Near(view.MultiplyPoint3x4(cameraPosition), Vector3::zero));
    CHECK(Near(view.MultiplyPoint3x4(cameraPosition + look * 4.0f), { 0.0f, 0.0f, 4.0f }));  // 視線の先は真正面

    // 透視投影: near で深度 0、far で深度 1
    const Matrix4x4 projection = Matrix4x4::PerspectiveFovLH(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    CHECK(Mathf::Approximately(projection.MultiplyPoint({ 0.0f, 0.0f, 0.1f }).z, 0.0f));
    CHECK(Mathf::Approximately(projection.MultiplyPoint({ 0.0f, 0.0f, 100.0f }).z, 1.0f));

    // UI 用の平行投影: 画面の左上 (0, 0) → (-1, 1)、右下 (1280, 720) → (1, -1)
    const Matrix4x4 ui = Matrix4x4::OrthographicOffCenterLH(0.0f, 1280.0f, 720.0f, 0.0f, 0.0f, 1.0f);
    CHECK(Near(ui.MultiplyPoint({ 0.0f, 0.0f, 0.0f }), { -1.0f, 1.0f, 0.0f }));
    CHECK(Near(ui.MultiplyPoint({ 1280.0f, 720.0f, 0.0f }), { 1.0f, -1.0f, 0.0f }));
}

static void TestColor() {
    CHECK(Near(Color::HSVToRGB(0.0f, 1.0f, 1.0f), Color::red));
    CHECK(Near(Color::HSVToRGB(1.0f / 3.0f, 1.0f, 1.0f), Color::green));
    float h = 0.0f, s = 0.0f, value = 0.0f;
    Color::RGBToHSV({ 0.2f, 0.4f, 0.8f }, h, s, value);
    CHECK(Near(Color::HSVToRGB(h, s, value), { 0.2f, 0.4f, 0.8f }));

    // sRGB の 0.5 はリニアでは約 0.214（見た目の半分の明るさは、光の量では 2 割ほど）
    CHECK(Mathf::Abs(Color{ 0.5f, 0.5f, 0.5f }.Linear().r - 0.21404f) < 1e-4f);
    CHECK(Near(Color{ 0.3f, 0.6f, 0.9f }.Linear().Gamma(), { 0.3f, 0.6f, 0.9f }));

    CHECK(Color32::FromColor({ 1.0f, 0.5f, 0.0f, 1.0f }) == (Color32{ 255, 128, 0, 255 }));
}

static void TestGeometry() {
    // Rect: 右端・上端はふくまない
    const Rect rect{ 0.0f, 0.0f, 10.0f, 5.0f };
    CHECK(rect.Contains({ 0.0f, 0.0f }) && !rect.Contains({ 10.0f, 2.0f }));
    CHECK(rect.Overlaps({ 9.0f, 4.0f, 5.0f, 5.0f }) && !rect.Overlaps({ 10.0f, 0.0f, 5.0f, 5.0f }));

    // Plane
    const Plane ground = Plane::FromNormalAndPoint(Vector3::up, { 0.0f, 1.0f, 0.0f });  // y = 1 の床
    CHECK(Mathf::Approximately(ground.GetDistanceToPoint({ 5.0f, 3.0f, 0.0f }), 2.0f));
    float enter = 0.0f;
    CHECK(ground.Raycast(Ray({ 0.0f, 5.0f, 0.0f }, Vector3::down), enter) && Mathf::Approximately(enter, 4.0f));
    CHECK(!ground.Raycast(Ray({ 0.0f, 5.0f, 0.0f }, Vector3::up), enter));  // 床から遠ざかる
    // 正面（-Z 側）から見て時計回りの 3 点 → 法線は手前（-Z）を向く
    const Plane fromPoints = Plane::FromPoints(Vector3::zero, Vector3::up, Vector3::right);
    CHECK(Near(fromPoints.normal, Vector3::back));

    // Bounds
    Bounds box = Bounds::FromMinMax({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });
    float distance = 0.0f;
    CHECK(box.IntersectRay(Ray({ 0.0f, 0.0f, -5.0f }, Vector3::forward), distance) && Mathf::Approximately(distance, 4.0f));
    CHECK(box.IntersectRay(Ray(Vector3::zero, Vector3::right), distance) && distance == 0.0f);  // 中から
    CHECK(!box.IntersectRay(Ray({ 0.0f, 3.0f, -5.0f }, Vector3::forward), distance));          // 上を素通り
    CHECK(!box.IntersectRay(Ray({ 0.0f, 0.0f, 5.0f }, Vector3::forward), distance));           // 箱は後ろ
    CHECK(Near(box.ClosestPoint({ 5.0f, 0.5f, 0.0f }), { 1.0f, 0.5f, 0.0f }));
    box.Encapsulate(Vector3{ 3.0f, 0.0f, 0.0f });  // {} だけだと Vector3 版と Bounds 版のどちらか決まらない
    CHECK(Near(box.Max(), { 3.0f, 1.0f, 1.0f }) && Near(box.Min(), { -1.0f, -1.0f, -1.0f }));

    // 45 度回すと、包む箱は √2 倍に広がる
    const Bounds unit = Bounds::FromCenterSize(Vector3::zero, Vector3::one * 2.0f);
    const Bounds rotated = unit.Transformed(Matrix4x4::TRS({ 5.0f, 0.0f, 0.0f }, Quaternion::Euler(0.0f, 45.0f, 0.0f), Vector3::one));
    CHECK(Near(rotated.center, { 5.0f, 0.0f, 0.0f }));
    CHECK(Near(rotated.extents, { Mathf::Sqrt(2.0f), 1.0f, Mathf::Sqrt(2.0f) }));

    // BoundingSphere
    const BoundingSphere sphere{ { 0.0f, 0.0f, 10.0f }, 2.0f };
    CHECK(sphere.IntersectRay(Ray(Vector3::zero, Vector3::forward), distance) && Mathf::Approximately(distance, 8.0f));
    CHECK(!sphere.IntersectRay(Ray({ 3.0f, 0.0f, 0.0f }, Vector3::forward), distance));
    CHECK(sphere.Intersects(BoundingSphere{ { 0.0f, 3.0f, 10.0f }, 1.5f }));
    CHECK(!sphere.Intersects(Bounds::FromMinMax({ 3.0f, 3.0f, 3.0f }, { 4.0f, 4.0f, 4.0f })));

    // Frustum: 原点から +Z を見るカメラ
    const Matrix4x4 viewProjection = Matrix4x4::View(Vector3::zero, Quaternion::identity)
                                   * Matrix4x4::PerspectiveFovLH(90.0f, 1.0f, 0.1f, 100.0f);
    const Frustum frustum = Frustum::FromMatrix(viewProjection);
    CHECK(frustum.Contains({ 0.0f, 0.0f, 10.0f }));
    CHECK(!frustum.Contains({ 0.0f, 0.0f, -10.0f }));  // 後ろ
    CHECK(!frustum.Contains({ 20.0f, 0.0f, 10.0f }));  // 右の外（90 度なので x ≤ z の範囲だけ写る）
    CHECK(!frustum.Contains({ 0.0f, 0.0f, 200.0f }));  // far より奥
    CHECK(Mathf::Approximately(frustum.planes[Frustum::Near].GetDistanceToPoint({ 0.0f, 0.0f, 1.1f }), 1.0f));
    CHECK(frustum.Intersects(Bounds::FromCenterSize({ 11.0f, 0.0f, 10.0f }, Vector3::one * 4.0f)));  // 端にかかる
    CHECK(!frustum.Intersects(Bounds::FromCenterSize({ 30.0f, 0.0f, 10.0f }, Vector3::one * 4.0f)));
    CHECK(!frustum.Intersects(BoundingSphere{ { 0.0f, 0.0f, -10.0f }, 5.0f }));

    // 三角形
    float u = 0.0f, v = 0.0f;
    const Vector3 a{ -1.0f, -1.0f, 5.0f }, b{ 0.0f, 1.0f, 5.0f }, c{ 1.0f, -1.0f, 5.0f };
    CHECK(Intersection::RayTriangle(Ray(Vector3::zero, Vector3::forward), a, b, c, distance, u, v));
    CHECK(Mathf::Approximately(distance, 5.0f));
    CHECK(Near(a + (b - a) * u + (c - a) * v, { 0.0f, 0.0f, 5.0f }));  // 重心座標から交点が戻る
    CHECK(!Intersection::RayTriangle(Ray({ 2.0f, 0.0f, 0.0f }, Vector3::forward), a, b, c, distance));
    CHECK(Near(Intersection::ClosestPointOnSegment({ 5.0f, 3.0f, 0.0f }, Vector3::zero, { 2.0f, 0.0f, 0.0f }), { 2.0f, 0.0f, 0.0f }));
}

static void TestRandom() {
    // 同じシードなら同じ並び
    RandomGenerator first(42);
    RandomGenerator second(42);
    bool same = true;
    for (int i = 0; i < 100; ++i) {
        same = same && first.NextUInt() == second.NextUInt();
    }
    CHECK(same);

    RandomGenerator random(7);
    bool inRange = true;
    int counts[3] = {};
    for (int i = 0; i < 30000; ++i) {
        const float f = random.Range(-2.0f, 3.0f);
        const int n = random.Range(0, 3);
        inRange = inRange && f >= -2.0f && f < 3.0f && n >= 0 && n < 3;
        if (n >= 0 && n < 3) {
            ++counts[n];
        }
        inRange = inRange && random.InsideUnitSphere().SqrMagnitude() <= 1.0f;
        inRange = inRange && Mathf::Abs(random.OnUnitSphere().Magnitude() - 1.0f) < 1e-4f;
        const Quaternion q = random.Rotation();
        inRange = inRange && Mathf::Abs(Quaternion::Dot(q, q) - 1.0f) < 1e-4f;
    }
    CHECK(inRange);
    // 偏りがないこと（各 10000 回前後）
    CHECK(counts[0] > 9500 && counts[1] > 9500 && counts[2] > 9500);
    CHECK(random.Range(5, 5) == 5);
}

static void TestEasing() {
    using Function = float (*)(float);
    const Function functions[] = {
        Easing::Linear, Easing::InQuad, Easing::OutQuad, Easing::InOutQuad, Easing::InCubic, Easing::OutCubic,
        Easing::InOutCubic, Easing::InSine, Easing::OutSine, Easing::InOutSine, Easing::InExpo, Easing::OutExpo,
        Easing::InBack, Easing::OutBack, Easing::OutElastic, Easing::OutBounce, Easing::InBounce,
    };
    bool endpoints = true;
    for (const Function f : functions) {
        endpoints = endpoints && Mathf::Abs(f(0.0f)) < 1e-5f && Mathf::Abs(f(1.0f) - 1.0f) < 1e-5f;
    }
    CHECK(endpoints);
    CHECK(Easing::OutBack(0.8f) > 1.0f);  // 行き過ぎる
}

static void TestHalf() {
    CHECK(Half::FromFloat(1.0f) == 0x3C00u);
    CHECK(Half::FromFloat(-2.0f) == 0xC000u);
    CHECK(Half::FromFloat(65504.0f) == 0x7BFFu);  // 最大値
    CHECK(Half::FromFloat(70000.0f) == 0x7C00u);  // 無限大へ
    CHECK(Half::ToFloat(Half::FromFloat(0.1f)) - 0.1f < 1e-4f);
    CHECK(Half::ToFloat(0x0001u) == 5.9604645e-8f);  // 一番小さい非正規化数 2⁻²⁴
    bool roundTrip = true;
    for (std::uint32_t bits = 0; bits < 0x7C00u; ++bits) {  // 有限の正の half すべて
        roundTrip = roundTrip && Half::FromFloat(Half::ToFloat(static_cast<std::uint16_t>(bits))) == bits;
    }
    CHECK(roundTrip);
}

int main() {
    TestMathf();
    TestVectors();
    TestQuaternion();
    TestMatrix();
    TestColor();
    TestGeometry();
    TestRandom();
    TestEasing();
    TestHalf();

    if (g_Failed == 0) {
        std::printf("all passed\n");
        return 0;
    }
    std::printf("%d failed\n", g_Failed);
    return 1;
}
