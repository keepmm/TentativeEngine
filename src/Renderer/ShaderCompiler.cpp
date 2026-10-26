/**
 * \file ShaderCompiler.cpp
 * \brief D3DCompile / dxc による HLSL のコンパイル
 */
#include "Renderer/ShaderCompiler.hpp"

#include "Core/Defines.hpp"
#include "Core/Log.hpp"

#include <d3dcompiler.h>
#if TE_RHI_VULKAN
#include <dxc/dxcapi.h>
#endif

#include <cstring>
#include <iterator>

namespace ShaderCompiler {

namespace {

// D3D11 / D3D12 用。シェーダーモデル 5.0 の DXBC を作る
std::vector<std::uint8_t> CompileDxbc(const std::string& source, const char* debugName, const char* entryPoint, Stage stage) {
    const char* target = stage == Stage::Vertex ? "vs_5_0" : "ps_5_0";

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

#if TE_RHI_VULKAN
// Vulkan 用。dxc で SPIR-V を作る（D3DCompile は SPIR-V を出せない）
std::vector<std::uint8_t> CompileSpirv(const std::string& source, const char* debugName, const char* entryPoint, Stage stage) {
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))
        || FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
        LogError("[Shader] dxc の初期化に失敗（dxcompiler.dll が exe の隣にあるか確認）");
        return {};
    }

    DxcBuffer sourceBuffer{};
    sourceBuffer.Ptr = source.data();
    sourceBuffer.Size = source.size();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    // dxc の引数はワイド文字列なので、エントリポイント名を変換しておく
    const std::wstring entry(entryPoint, entryPoint + std::strlen(entryPoint));
    LPCWSTR arguments[] = {
        L"-E", entry.c_str(),
        L"-T", stage == Stage::Vertex ? L"vs_6_0" : L"ps_6_0",
        L"-spirv",
        L"-fspv-target-env=vulkan1.3",
#ifdef _DEBUG
        L"-Od",  // Debug は最適化しない（RenderDoc などで追いやすくするため）
#else
        L"-O3",
#endif
    };

    ComPtr<IDxcResult> result;
    if (FAILED(compiler->Compile(&sourceBuffer, arguments, static_cast<UINT32>(std::size(arguments)), nullptr,
                                 IID_PPV_ARGS(&result)))) {
        LogError(std::string("[Shader] dxc の呼び出しに失敗: ") + debugName);
        return {};
    }

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status)) {
        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        LogError(std::string("[Shader] コンパイルに失敗: ") + debugName + "\n"
                 + (errors && errors->GetStringLength() > 0 ? errors->GetStringPointer() : ""));
        return {};
    }

    ComPtr<IDxcBlob> code;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), nullptr);
    if (!code) {
        return {};
    }
    std::vector<std::uint8_t> bytes(code->GetBufferSize());
    std::memcpy(bytes.data(), code->GetBufferPointer(), bytes.size());
    return bytes;
}
#endif

}  // namespace

std::vector<std::uint8_t> Compile(
    const std::string& source,
    const char* debugName,
    const char* entryPoint,
    Stage stage,
    ERHIBackend backend) {
    if (backend == ERHIBackend::Vulkan) {
#if TE_RHI_VULKAN
        return CompileSpirv(source, debugName, entryPoint, stage);
#else
        LogError("[Shader] Vulkan なしでビルドしているので SPIR-V は作れない");
        return {};
#endif
    }
    return CompileDxbc(source, debugName, entryPoint, stage);
}

}  // namespace ShaderCompiler
