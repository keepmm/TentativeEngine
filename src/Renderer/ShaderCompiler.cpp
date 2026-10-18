/**
 * \file ShaderCompiler.cpp
 * \brief D3DCompile による HLSL のコンパイル
 */
#include "Renderer/ShaderCompiler.hpp"

#include "Core/Defines.hpp"
#include "Core/Log.hpp"

#include <d3dcompiler.h>

#include <cstring>

namespace ShaderCompiler {

std::vector<std::uint8_t> Compile(
    const std::string& source,
    const char* debugName,
    const char* entryPoint,
    const char* target) {
    // Debug ビルドでは最適化を切ってデバッグ情報を残す。PIX などでシェーダーを追えるようにするため
#ifdef _DEBUG
    const UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> error;
    const HRESULT hr = D3DCompile(
        source.data(), source.size(), debugName, nullptr, nullptr,
        entryPoint, target, flags, 0, &code, &error);
    if (FAILED(hr)) {
        LogError(std::string("[Shader] コンパイルに失敗: ") + debugName + "\n"
                 + (error ? static_cast<const char*>(error->GetBufferPointer()) : ""));
        return {};
    }

    std::vector<std::uint8_t> bytes(code->GetBufferSize());
    std::memcpy(bytes.data(), code->GetBufferPointer(), bytes.size());
    return bytes;
}

}  // namespace ShaderCompiler
