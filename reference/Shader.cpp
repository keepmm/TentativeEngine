#include "Shader.hpp"
#include <fstream>

namespace
{
	// キャッシュの置き場。ビルド成果物なので Git には入れない
	// プロジェクト側に置く（プロジェクトごとにシェーダー構成が違いうるため）
	constexpr const wchar_t* kShaderCacheDir = L"Assets/ShaderCache";
	constexpr uint32_t kShaderCacheMagic = 0x31534843;	// 'CHS1'

	/// @brief シェーダーソース全体の最終更新時刻
	/// @note インクルード関係を追う代わりに、.hlsl/.hlsli のどれか1つでも
	///       触られたら全キャッシュを捨てる。確実だし実運用で困らない
	uint64_t ShaderSourceStamp(const std::wstring& filepath)
	{
		std::filesystem::path dir = std::filesystem::path(filepath).parent_path();
		if (dir.empty()) dir = L".";

		uint64_t newest = 0;
		std::error_code ec;
		for (const auto& e : std::filesystem::directory_iterator(dir, ec))
		{
			if (ec) break;
			if (!e.is_regular_file(ec)) continue;

			const auto ext = e.path().extension();
			if (ext != L".hlsl" && ext != L".hlsli") continue;

			const auto t = std::filesystem::last_write_time(e.path(), ec);
			if (ec) continue;

			const uint64_t v = static_cast<uint64_t>(t.time_since_epoch().count());
			if (v > newest) newest = v;
		}
		return newest;
	}

	/// @brief 入力の組み合わせごとに一意なキャッシュファイル名を作る
	std::wstring ShaderCachePath(const std::wstring& filepath, const std::string& entryPoint,
		const std::string& profile, UINT compileFlags)
	{
		std::wstring key = filepath;
		key += L'|';
		key.append(entryPoint.begin(), entryPoint.end());
		key += L'|';
		key.append(profile.begin(), profile.end());
		key += L'|';
		key += std::to_wstring(compileFlags);

		wchar_t name[64];
		swprintf_s(name, L"%016llx.cso",
			static_cast<unsigned long long>(std::hash<std::wstring>{}(key)));

		return std::wstring(kShaderCacheDir) + L"/" + name;
	}

	bool LoadCachedBlob(const std::wstring& path, uint64_t stamp, ComPtr<ID3DBlob>& out)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs) return false;

		uint32_t magic = 0;
		uint64_t cachedStamp = 0;
		uint32_t size = 0;
		ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		ifs.read(reinterpret_cast<char*>(&cachedStamp), sizeof(cachedStamp));
		ifs.read(reinterpret_cast<char*>(&size), sizeof(size));
		if (!ifs || magic != kShaderCacheMagic || cachedStamp != stamp || size == 0)
		{
			return false;
		}

		ComPtr<ID3DBlob> blob;
		if (FAILED(D3DCreateBlob(size, &blob))) return false;

		ifs.read(static_cast<char*>(blob->GetBufferPointer()), size);
		if (!ifs) return false;

		out = blob;
		return true;
	}

	void SaveCachedBlob(const std::wstring& path, uint64_t stamp, ID3DBlob* blob)
	{
		if (blob == nullptr) return;

		std::error_code ec;
		std::filesystem::create_directories(kShaderCacheDir, ec);

		std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
		if (!ofs) return;

		const uint32_t magic = kShaderCacheMagic;
		const uint32_t size = static_cast<uint32_t>(blob->GetBufferSize());
		ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
		ofs.write(reinterpret_cast<const char*>(&stamp), sizeof(stamp));
		ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
		ofs.write(static_cast<const char*>(blob->GetBufferPointer()), size);
	}
}

bool UseDxcProfile(const std::string& profile)
{
	return profile.find("6_") != std::string::npos ||
		profile.find("ms_", 0) == 0 ||
		profile.find("as_", 0) == 0;
}

using DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

HRESULT CreateDxcInstance(REFCLSID clsid, REFIID iid, void** ppv)
{
	static HMODULE module = LoadLibraryW(L"dxcompiler.dll");
	if (module == nullptr)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	auto proc = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module, "DxcCreateInstance"));
	if (proc == nullptr)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	return proc(clsid, iid, ppv);
}

bool Shader::LoadFromFile(
	const std::wstring& filepath,
	const std::string& entryPoint,
	const std::string& profile,
	UINT compileFlags)
{
	m_Blob.Reset();
	m_DxcBlob.Reset();

	std::wstring resolved = filepath;
	if (!std::filesystem::exists(resolved))
	{
		std::wstring alt = L"Shaders/" +
			std::wstring(std::filesystem::path(filepath).filename());
		if (std::filesystem::exists(alt)) resolved = alt;
	}

	if (UseDxcProfile(profile))
	{
		return CompileWithDxc(resolved, entryPoint, profile, compileFlags);
	}

	return CompileWithD3DCompile(resolved, entryPoint, profile, compileFlags);
}

bool Shader::CompileWithDxc(const std::wstring& filepath, const std::string& entryPoint, const std::string& profile, UINT compileFlags)
{
	ComPtr<IDxcUtils> utils;
	ComPtr<IDxcCompiler3> compiler;

	if (FAILED(CreateDxcInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))))
	{
		return false;
	}

	if(FAILED(CreateDxcInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
	{
		return false;
	}

	ComPtr<IDxcBlobEncoding> source;
	if(FAILED(utils->LoadFile(filepath.c_str(), nullptr, &source)))
	{
		return false;
	}

	DxcBuffer buffer{};
	buffer.Ptr = source->GetBufferPointer();
	buffer.Size = source->GetBufferSize();
	buffer.Encoding = DXC_CP_UTF8;

	std::wstring entryPointW(entryPoint.begin(), entryPoint.end());
	std::wstring profileW(profile.begin(), profile.end());

	std::vector<LPCWSTR> args;
	args.push_back(L"-E"); args.push_back(entryPointW.c_str());
	args.push_back(L"-T"); args.push_back(profileW.c_str());
	args.push_back(L"-HV"); args.push_back(L"2021");

#ifdef _SHADER_DEBUG
	args.push_back(L"-Zi");
	args.push_back(L"-Qembed_debug");
	args.push_back(L"-Od");
#else
	args.push_back(L"-O3");
#endif
	ComPtr<IDxcIncludeHandler> includeHandler;
	if (FAILED(utils->CreateDefaultIncludeHandler(&includeHandler)))
	{
		return false;
	}

	ComPtr<IDxcResult> result;
	if (FAILED(compiler->Compile(
		&buffer,
		args.data(),
		static_cast<UINT>(args.size()),
		includeHandler.Get(),
		IID_PPV_ARGS(&result))))
	{
		return false;
	}

	ComPtr<IDxcBlobUtf8> errors;
	if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)))
	{
		if (errors != nullptr && errors->GetStringLength() > 0)
		{
			OutputDebugStringA(errors->GetStringPointer());
		}
	}

	if (errors && errors->GetStringLength() > 0)
	{
		m_LastError = errors->GetStringPointer();	// 保持
		OutputDebugStringA(m_LastError.c_str());
	}

	HRESULT status = S_OK;
	if (FAILED(result->GetStatus(&status)) || FAILED(status))
	{
		return false;
	}

	if(FAILED(result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&m_DxcBlob), nullptr)))
	{
		return false;
	}

	return true;
}

bool Shader::CompileWithD3DCompile(const std::wstring& filepath, const std::string& entryPoint, const std::string& profile, UINT compileFlags)
{
	if (compileFlags == 0)
	{
#ifdef _SHADER_DEBUG
		compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
	}

	// 最適化ありのコンパイルは重い。ソースが変わっていなければ前回の結果を使う
	const uint64_t stamp = ShaderSourceStamp(filepath);
	const std::wstring cachePath = ShaderCachePath(filepath, entryPoint, profile, compileFlags);
	if (LoadCachedBlob(cachePath, stamp, m_Blob))
	{
		return true;
	}

	ComPtr<ID3DBlob> errorBlob;
	const auto hr = D3DCompileFromFile(
		filepath.c_str(),
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint.c_str(),
		profile.c_str(),
		compileFlags,
		0,
		&m_Blob,
		&errorBlob);

	if (FAILED(hr))
	{
		if (errorBlob != nullptr)
		{
			OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		return false;
	}

	if (errorBlob)
	{
		m_LastError = static_cast<const char*>(errorBlob->GetBufferPointer());
	}

	SaveCachedBlob(cachePath, stamp, m_Blob.Get());

	return true;
}
