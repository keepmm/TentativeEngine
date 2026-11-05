#include "VulkanBinding.hlsli"

VK_BINDING(0,1)    Texture2D g_Texture    : register(t0);
VK_BINDING(100,0)  SamplerState g_Sampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    VK_LOCATION(0)  float2 uv : TEXCOORD0;
};

// 画面全体を覆う大きな三角形 1 枚。頂点バッファは使わない
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    const float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    VSOutput output;
    output.position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    output.uv = uv;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return g_Texture.Sample(g_Sampler, input.uv);
}