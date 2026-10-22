/**
 * \file MathCompareTests.cpp
 * \brief DirectXMath と同じ値になるかの答え合わせ（テスト専用。本体は DirectXMath に依存しない）
 */
#include "Math/Math.hpp"

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include <cstdio>

using namespace TE;
using namespace DirectX;

namespace {

int g_Failed = 0;

void Compare(const char* name, const Matrix4x4& actual, const XMMATRIX& expectedMatrix) {
    XMFLOAT4X4 expected;
    XMStoreFloat4x4(&expected, expectedMatrix);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const float a = actual.m[row][col];
            const float e = expected.m[row][col];
            if (Mathf::Abs(a - e) > 1e-4f * Mathf::Max(1.0f, Mathf::Abs(e))) {
                std::printf("FAILED: %s [%d][%d] %f != %f\n", name, row, col, a, e);
                ++g_Failed;
            }
        }
    }
}

XMVECTOR ToXM(const Vector3& v, float w) { return XMVectorSet(v.x, v.y, v.z, w); }

}  // namespace

int main() {
    const float pitch = 23.0f, yaw = -70.0f, roll = 51.0f;
    const Quaternion q = Quaternion::Euler(pitch, yaw, roll);
    const XMVECTOR qx = XMQuaternionRotationRollPitchYaw(pitch * Mathf::Deg2Rad, yaw * Mathf::Deg2Rad, roll * Mathf::Deg2Rad);

    Compare("Euler", Matrix4x4::Rotate(q), XMMatrixRotationQuaternion(qx));
    Compare("AngleAxis", Matrix4x4::Rotate(Quaternion::AngleAxis(40.0f, { 1.0f, 2.0f, 3.0f })),
            XMMatrixRotationAxis(XMVectorSet(1.0f, 2.0f, 3.0f, 0.0f), 40.0f * Mathf::Deg2Rad));

    // XMQuaternionMultiply(Q1, Q2) は「Q1 の後に Q2」= ハミルトン積 Q2 * Q1
    const Quaternion a = Quaternion::AngleAxis(17.0f, Vector3::right);
    const Quaternion b = Quaternion::AngleAxis(63.0f, { 0.0f, 1.0f, 1.0f });
    const XMVECTOR ax = XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), 17.0f * Mathf::Deg2Rad);
    const XMVECTOR bx = XMQuaternionRotationAxis(XMVectorSet(0.0f, 1.0f, 1.0f, 0.0f), 63.0f * Mathf::Deg2Rad);
    Compare("Multiply", Matrix4x4::Rotate(a * b), XMMatrixRotationQuaternion(XMQuaternionMultiply(bx, ax)));

    const Matrix4x4 trs = Matrix4x4::TRS({ 1.0f, 2.0f, 3.0f }, q, { 2.0f, 3.0f, 4.0f });
    const XMMATRIX trsx = XMMatrixScaling(2.0f, 3.0f, 4.0f) * XMMatrixRotationQuaternion(qx) * XMMatrixTranslation(1.0f, 2.0f, 3.0f);
    Compare("TRS", trs, trsx);
    Compare("Inverse", trs.Inverse(), XMMatrixInverse(nullptr, trsx));
    const float determinant = trs.Determinant();
    const float expectedDeterminant = XMVectorGetX(XMMatrixDeterminant(trsx));
    if (Mathf::Abs(determinant - expectedDeterminant) > 1e-3f) {
        std::printf("FAILED: Determinant %f != %f\n", determinant, expectedDeterminant);
        ++g_Failed;
    }

    const Vector3 eye{ 3.0f, 2.0f, -5.0f };
    Compare("LookAt", Matrix4x4::LookAtLH(eye, { 0.0f, 1.0f, 0.0f }, Vector3::up),
            XMMatrixLookAtLH(ToXM(eye, 1.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));

    // LookRotation の全分岐（どの対角成分が一番大きいか）を、View 行列どうしで比べる
    const Vector3 directions[] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.1f, -1.0f }, { -1.0f, 0.2f, -0.2f },
        { 0.1f, -0.3f, -1.0f }, { 0.0f, 0.5f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { -3.0f, -1.0f, 5.0f },
    };
    for (const Vector3& direction : directions) {
        const Vector3 d = direction.Normalized();
        Compare("View(LookRotation)", Matrix4x4::View(eye, Quaternion::LookRotation(d)),
                XMMatrixLookToLH(ToXM(eye, 1.0f), ToXM(d, 0.0f), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
    }

    Compare("Perspective", Matrix4x4::PerspectiveFovLH(60.0f, 16.0f / 9.0f, 0.1f, 100.0f),
            XMMatrixPerspectiveFovLH(60.0f * Mathf::Deg2Rad, 16.0f / 9.0f, 0.1f, 100.0f));
    Compare("Orthographic", Matrix4x4::OrthographicLH(20.0f, 10.0f, 0.5f, 50.0f), XMMatrixOrthographicLH(20.0f, 10.0f, 0.5f, 50.0f));
    Compare("OrthographicOffCenter", Matrix4x4::OrthographicOffCenterLH(0.0f, 1280.0f, 720.0f, 0.0f, 0.0f, 1.0f),
            XMMatrixOrthographicOffCenterLH(0.0f, 1280.0f, 720.0f, 0.0f, 0.0f, 1.0f));

    // sRGB 変換
    for (float value = 0.0f; value <= 1.0f; value += 0.01f) {
        const float expected = XMVectorGetX(XMColorSRGBToRGB(XMVectorSet(value, value, value, 1.0f)));
        if (Mathf::Abs(Color::GammaToLinearSpace(value) - expected) > 1e-4f) {
            std::printf("FAILED: GammaToLinear(%f) %f != %f\n", value, Color::GammaToLinearSpace(value), expected);
            ++g_Failed;
        }
    }

    // half 変換（丸めの境目をふくむ、たくさんの値で比べる）
    int halfMismatches = 0;
    for (std::uint32_t i = 0; i < 2000000; ++i) {
        const float value = (static_cast<float>(i) - 1000000.0f) * 0.07f + static_cast<float>(i % 7) * 1e-6f;
        if (Half::FromFloat(value) != PackedVector::XMConvertFloatToHalf(value)) {
            if (++halfMismatches <= 5) {
                std::printf("FAILED: Half(%g) %04X != %04X\n", value, Half::FromFloat(value), PackedVector::XMConvertFloatToHalf(value));
            }
        }
    }
    g_Failed += halfMismatches;

    if (g_Failed == 0) {
        std::printf("compare passed\n");
        return 0;
    }
    std::printf("%d failed\n", g_Failed);
    return 1;
}
