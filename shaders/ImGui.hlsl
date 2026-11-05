// ImGui の描画。頂点は ImDrawVert（位置 float2 / UV float2 / 色 RGBA8）そのまま。
// ImGui の色は sRGB の値のまま合成する前提。バックバッファは _SRGB ではない（R8G8B8A8_UNORM）ので、変換せずに書く
#include "VulkanBinding.hlsli"

// C++ の ImGuiConstants（ImGuiRenderer.cpp）と同じ並び。画面の座標（ピクセル）→ NDC
VK_BINDING(0, 0) cbuffer ImGuiConstants : register(b0)
{
    float2 g_Scale;
    float2 g_Translate;
};

VK_BINDING(0, 1)   Texture2D    g_Texture : register(t0);
VK_BINDING(100, 0) SamplerState g_Sampler : register(s0);

struct VSInput
{
    VK_LOCATION(0) float2 position : POSITION;
    VK_LOCATION(1) float2 uv       : TEXCOORD0;
    VK_LOCATION(2) float4 color    : COLOR0;     // R8G8B8A8_UNORM なので 0〜1 で届く
};

struct VSOutput
{
    float4 position               : SV_Position;
    VK_LOCATION(0) float2 uv      : TEXCOORD0;
    VK_LOCATION(1) float4 color   : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position * g_Scale + g_Translate, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color * g_Texture.Sample(g_Sampler, input.uv);
}
