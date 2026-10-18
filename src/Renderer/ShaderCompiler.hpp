/**
 * \file ShaderCompiler.hpp
 * \brief HLSL ソースをバイトコードにコンパイルする
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// RHI はコンパイル済みのバイト列しか受け取らない（RHIShaderByteCode）。
// HLSL → バイト列の変換はここに閉じ込めて、d3dcompiler への依存をこのファイルだけにする。
// Vulkan を足すときは、ここを DXC（-spirv）に差し替える
namespace ShaderCompiler {

/// @brief HLSL ソースをコンパイルする
/// @param target "vs_5_0" / "ps_5_0" など
/// @return バイトコード。失敗したら空（エラー内容はログに出す）
std::vector<std::uint8_t> Compile(
    const std::string& source,
    const char* debugName,
    const char* entryPoint,
    const char* target);

}  // namespace ShaderCompiler
