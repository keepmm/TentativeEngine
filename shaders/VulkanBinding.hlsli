// Vulkan 用の注釈のマクロだけ。Common.hlsli から切り出したもの。
// FrameConstants（b0）を持ち込みたくないシェーダー（ImGui.hlsl）もこれだけ include できるようにする
#ifndef TE_VULKAN_BINDING_HLSLI
#define TE_VULKAN_BINDING_HLSLI

// Vulkan（SPIR-V）のときだけ __spirv__ が定義されるので、Vulkan 用の注釈はそのときだけ付ける。
//   VK_BINDING(b, s) : RHI の Vulkan バックエンドは「レイアウトのスロット番号 = set」「レジスタ番号 = binding」
//   VK_LOCATION(n)   : 頂点属性は vertexAttributes に並べた順番が location になる
//   マクロの引数名を binding / location にすると、属性名の vk::binding まで置き換わって壊れる
#ifdef __spirv__
#define VK_BINDING(b, s) [[vk::binding(b, s)]]
#define VK_LOCATION(n)   [[vk::location(n)]]
#else
#define VK_BINDING(b, s)
#define VK_LOCATION(n)
#endif

#endif
