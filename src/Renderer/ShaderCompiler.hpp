/**
 * \file ShaderCompiler.hpp
 * \brief HLSL ソースをバイトコードにコンパイルする
 */
#pragma once

#include "RHI/RHITypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// RHI はコンパイル済みのバイト列しか受け取らない（RHIShaderByteCode）。
// HLSL → バイト列の変換はここに閉じ込めて、コンパイラ（d3dcompiler / dxc）への依存をこのファイルだけにする。
//   D3D11 / D3D12 : D3DCompile で DXBC（シェーダーモデル 5.0）
//   Vulkan        : dxc で SPIR-V（シェーダーモデル 6.0）
// 同じ HLSL を両方で使えるよう、Vulkan のときは __spirv__ が定義される（[[vk::binding]] などを書き分けるため）
namespace ShaderCompiler {

enum class Stage : std::uint8_t {
    Vertex,
    Pixel,
};

/// @brief HLSL ソースを、backend が読める形にコンパイルする
/// @return バイトコード。失敗したら空（エラー内容はログに出す）
std::vector<std::uint8_t> Compile(
    const std::string& source,
    const char* debugName,
    const char* entryPoint,
    Stage stage,
    ERHIBackend backend);

}  // namespace ShaderCompiler
