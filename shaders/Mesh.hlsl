// メッシュの描画（インスタンシング + テクスチャ 3 枚 + ランバート）
// VK_BINDING / VK_LOCATION と FrameConstants（b0）は Common.hlsli にある
#include "Common.hlsli"

// マテリアル。スロット 2 なので Vulkan では set 2
VK_BINDING(2, 2)
cbuffer MaterialConstants : register(b2)
{
    float4 g_BaseColorFactor;
    float4 g_EmissiveFactor;
};

// テクスチャはスロット 1 のテーブル（Vulkan では set 1、binding = レジスタ番号）。
// sRGB の画像（ベースカラー・エミッシブ）は、読んだ時点でリニアな値になっている
VK_BINDING(0, 1)  Texture2D g_BaseColorMap : register(t0);
VK_BINDING(1, 1)  Texture2D g_NormalMap : register(t1);
VK_BINDING(2, 1)  Texture2D g_EmissiveMap : register(t2);
// 静的サンプラーは、Vulkan では set 0 の binding 100 + レジスタ番号に入る（RHI の決まり）
VK_BINDING(100, 0)  SamplerState g_Sampler : register(s0);

struct VSInput
{
    VK_LOCATION(0)  float3 position : POSITION;
    VK_LOCATION(1)  float3 normal : NORMAL;
    VK_LOCATION(2)  float2 uv : TEXCOORD0;
    VK_LOCATION(3)  float3 tangent : TANGENT;
    VK_LOCATION(4)  float4 world0 : WORLD0;
    VK_LOCATION(5)  float4 world1 : WORLD1;
    VK_LOCATION(6)  float4 world2 : WORLD2;
    VK_LOCATION(7)  float4 world3 : WORLD3;
    VK_LOCATION(8)  float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 tangent : TANGENT;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    // 4 つの行を並べると、C++ の Matrix4x4（m[行][列]）と同じ並びの行列になる。転置はいらない
    const float4x4 world = float4x4(input.world0, input.world1, input.world2, input.world3);

    PSInput output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), world);
    output.position = mul(worldPosition, g_ViewProjection);
    // 向きなので w = 0。拡大率が軸ごとに違う物体を扱うときは、法線用の行列（NormalMatrix）に替える
    output.normal = mul(float4(input.normal, 0.0f), world).xyz;
    output.tangent = mul(float4(input.tangent, 0.0f), world).xyz;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_Target
{
    // テクスチャは分岐の外で読む（分岐の中だと、ミップを選ぶための微分が正しく取れない）
    const float4 baseColorTex = g_BaseColorMap.Sample(g_Sampler, input.uv);
    const float3 normalTex = g_NormalMap.Sample(g_Sampler, input.uv).xyz;
    const float3 emissiveTex = g_EmissiveMap.Sample(g_Sampler, input.uv).rgb;

    // ---- 法線マップ ----
    // 補間すると法線と接線が直交しなくなるので、接線から法線方向の成分を取り除く（グラム・シュミット）
    float3 normal = normalize(input.normal);
    float3 tangent = input.tangent - normal * dot(input.tangent, normal);
    // 接線を持たないモデル（長さ 0）では、法線マップを使わず頂点の法線のままにする
    if (dot(tangent, tangent) > 1e-8f)
    {
        tangent = normalize(tangent);
        // 左手系なので cross(法線, 接線) が V の増える向き（画像の下向き）になる。DirectX 形式の法線マップに合う
        const float3 bitangent = cross(normal, tangent);
        // 画像の 0〜1 を -1〜1 に戻し、接空間（T, B, N）からワールド空間へ
        const float3 n = normalTex * 2.0f - 1.0f;
        normal = normalize(n.x * tangent + n.y * bitangent + n.z * normal);
    }

    // ---- 色 ----
    // ベースカラー = テクスチャ × マテリアルの係数 × 物体ごとの色（MeshRenderer）
    const float4 baseColor = baseColorTex * g_BaseColorFactor * input.color;
    const float3 emissive = emissiveTex * g_EmissiveFactor.rgb;

    // ランバート反射: 面が光の来る方を向いているほど明るい。真っ暗にならないよう環境光を少し足す
    const float diffuse = saturate(dot(normal, -g_LightDirection.xyz));
    float3 color = baseColor.rgb * (0.25f + 0.75f * diffuse) + emissive;

    // ここまではリニアな値で計算した。スワップチェインは UNORM で sRGB 変換をしないので、
    // 画面に出す前にガンマ補正する（しないと暗く見える）
    color = pow(saturate(color), 1.0f / 2.2f);
    return float4(color, baseColor.a);
}