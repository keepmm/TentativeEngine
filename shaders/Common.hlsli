// Mesh.hlsl と Grid.hlsl で共有する定義。
// 同じファイルを 2 回 #include しても二重定義にならないようにガードを付ける
#ifndef TE_COMMON_HLSLI
#define TE_COMMON_HLSLI

// VK_BINDING / VK_LOCATION は VulkanBinding.hlsli にある（ImGui.hlsl も使うため切り出した）
#include "VulkanBinding.hlsli"

// C++ の FrameConstants（Renderer.cpp）と同じ並びにする。行列は C++ 側で Transposed() してある（cbuffer は列優先で読むため）
VK_BINDING(0, 0) cbuffer FrameConstants : register(b0)
{
    float4x4 g_ViewProjection;
    float4   g_LightDirection;         // xyz: 光が進む向き（ワールド空間）
    float4x4 g_InverseViewProjection;  // 画面上の点をワールドに戻す（グリッドが使う）
    float4   g_CameraPosition;         // xyz: カメラのワールド位置
};

#endif
