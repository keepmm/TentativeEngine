/**
 * \file ShaderCompiler.cpp
 * \brief D3DCompile / dxc による HLSL のコンパイル
 */
#include "Renderer/ShaderCompiler.hpp"

#include "Core/Platform.hpp"

#if TE_PLATFORM_WINDOWS

#include "Core/Defines.hpp"
#include "Core/Log.hpp"

#include <d3dcompiler.h>
#if TE_RHI_VULKAN
#include <dxc/dxcapi.h>
#endif

#include <cstring>
#include <fstream>
#include <iterator>

namespace ShaderCompiler {

namespace {

// ファイルを丸ごと読む。先頭の BOM（EF BB BF）は外す。
// D3DCompile は BOM を文字として読んでしまい、「X3000: Illegal character」で失敗するため
bool ReadSourceFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (out.size() >= 3
        && static_cast<unsigned char>(out[0]) == 0xEF
        && static_cast<unsigned char>(out[1]) == 0xBB
        && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

// D3DCompile 用の #include の処理役。directory から探して読む。
// 標準の D3D_COMPILE_STANDARD_FILE_INCLUDE は BOM を外さないうえ、
// メモリから渡したソースだと探す場所が作業ディレクトリになることがあるので、自前で用意する
class DirectoryInclude final : public ID3DInclude {
public:
    explicit DirectoryInclude(std::filesystem::path directory) : m_Directory(std::move(directory)) {}

    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID* data, UINT* bytes) override {
        std::string text;
        if (!ReadSourceFile(m_Directory / fileName, text)) {
            return E_FAIL;
        }
        // コンパイラが Close を呼ぶまで生きていればよい。new で確保して Close で消す
        char* copy = new char[text.size()];
        std::memcpy(copy, text.data(), text.size());
        *data = copy;
        *bytes = static_cast<UINT>(text.size());
        return S_OK;
    }

    HRESULT __stdcall Close(LPCVOID data) override {
        delete[] static_cast<const char*>(data);
        return S_OK;
    }

private:
    std::filesystem::path m_Directory;
};

// D3D11 / D3D12 用。シェーダーモデル 5.0 の DXBC を作る
std::vector<std::uint8_t> CompileDxbc(const std::string& source, const char* debugName, const char* entryPoint,
                                      Stage stage, ID3DInclude* include) {
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
        source.data(), source.size(), debugName, nullptr, include,
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
// Vulkan 用。dxc で SPIR-V を作る（D3DCompile は SPIR-V を出せない）。
// sourcePath が空でなければ、エラーに出る名前と #include を探すフォルダとして使う
std::vector<std::uint8_t> CompileSpirv(const std::string& source, const char* debugName, const char* entryPoint,
                                       Stage stage, const std::filesystem::path& sourcePath) {
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
    std::vector<LPCWSTR> arguments = {
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

    // 文字列は arguments が指している間（Compile が終わるまで）生きている必要があるので、外で持つ
    const std::wstring sourceName = sourcePath.wstring();
    const std::wstring includeDirectory = sourcePath.parent_path().wstring();
    ComPtr<IDxcIncludeHandler> includeHandler;
    if (!sourcePath.empty()) {
        arguments.push_back(sourceName.c_str());  // 「-」で始まらない引数は、ソースのファイル名として扱われる
        arguments.push_back(L"-I");
        arguments.push_back(includeDirectory.c_str());
        utils->CreateDefaultIncludeHandler(&includeHandler);
    }

    ComPtr<IDxcResult> result;
    if (FAILED(compiler->Compile(&sourceBuffer, arguments.data(), static_cast<UINT32>(arguments.size()),
                                 includeHandler.Get(), IID_PPV_ARGS(&result)))) {
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

// backend に合わせて振り分ける。sourcePath が空ならメモリ上のソース（#include は使えない）
std::vector<std::uint8_t> CompileFor(const std::string& source, const char* debugName, const char* entryPoint,
                                     Stage stage, ERHIBackend backend, const std::filesystem::path& sourcePath) {
    if (backend == ERHIBackend::Vulkan) {
#if TE_RHI_VULKAN
        return CompileSpirv(source, debugName, entryPoint, stage, sourcePath);
#else
        LogError("[Shader] Vulkan なしでビルドしているので SPIR-V は作れない");
        return {};
#endif
    }
    if (sourcePath.empty()) {
        return CompileDxbc(source, debugName, entryPoint, stage, nullptr);
    }
    DirectoryInclude include(sourcePath.parent_path());
    return CompileDxbc(source, debugName, entryPoint, stage, &include);
}

}  // namespace

std::vector<std::uint8_t> Compile(
    const std::string& source,
    const char* debugName,
    const char* entryPoint,
    Stage stage,
    ERHIBackend backend) {
    return CompileFor(source, debugName, entryPoint, stage, backend, {});
}

std::vector<std::uint8_t> CompileFile(
    const std::filesystem::path& path,
    const char* entryPoint,
    Stage stage,
    ERHIBackend backend) {
    std::string source;
    if (!ReadSourceFile(path, source)) {
        LogError("[Shader] ファイルを開けません: " + path.string());
        return {};
    }
    // エラーの行頭に出る名前。フルパスにしておくと、出力ウィンドウでダブルクリックしてその行へ飛べる
    const std::string name = path.string();
    return CompileFor(source, name.c_str(), entryPoint, stage, backend, path);
}

}  // namespace ShaderCompiler

#else  // Android: 実行時にはコンパイルせず、Gradle が作った .spv を読む

#include "Core/Log.hpp"

#include <fstream>
#include <iterator>

namespace ShaderCompiler {

std::vector<std::uint8_t> Compile(const std::string&, const char* debugName, const char*, Stage, ERHIBackend) {
    LogError(std::string("[Shader] Android ではソースからコンパイルできない: ") + debugName);
    return {};
}

std::vector<std::uint8_t> CompileFile(const std::filesystem::path& path, const char* entryPoint, Stage, ERHIBackend) {
    // Mesh.hlsl + VSMain → Mesh.VSMain.spv
    std::filesystem::path spv = path;
    spv.replace_extension(std::string(".") + entryPoint + ".spv");
    std::ifstream file(spv, std::ios::binary);
    if (!file) {
        LogError("[Shader] ファイルを開けません: " + spv.string());
        return {};
    }
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

}  // namespace ShaderCompiler

#endif
