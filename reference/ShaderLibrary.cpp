#include "ShaderLibrary.hpp"
#include "Util.hpp"
#include <filesystem>

namespace
{
	/// @brief シェーダーのパスを解決する
	/// @note プロジェクト側(Assets/Shaders 等)を優先し、無ければ exe 横の EngineAssets/Shaders を見る。
	///       ビルトインは EngineAssets 側にあるので、CWD がユーザープロジェクトへ移っても解決できる
	std::wstring ResolveShaderPath(const std::wstring& filePath)
	{
		const std::filesystem::path p = filePath;
		if (p.is_absolute() || std::filesystem::exists(p))
		{
			return filePath;
		}
		return EngineAssetPath(std::filesystem::path(L"Shaders") / p).wstring();
	}
}


const Shader* ShaderLibrary::Load(
    const std::wstring& filePath, 
    const std::string& entryPoint, 
    const std::string& profile, 
    UINT compileFlags)
{

	// ビルトインは EngineAssets 側にあるので、キーは解決後のパスで作る
	const std::wstring path = ResolveShaderPath(filePath);

	// すでに同じShaderKeyが存在するか確認
	const ShaderKey key{ path , entryPoint, profile, compileFlags };

    // 存在する場合はキャッシュを返す
	auto it = m_Shaders.find(key);
    if (it != m_Shaders.end())
    {
        return it->second.shader.get();
	}

    // 存在しない場合は新規に作成
	auto shader = std::make_shared<Shader>();
    if (!shader->LoadFromFile(
        path,
        entryPoint,
        profile,
        compileFlags))
    {
        return nullptr;
    }

	ShaderEntry entry{};
    entry.shader = shader;

    if(std::filesystem::exists(path))
    {
        entry.lastWriteTime = std::filesystem::last_write_time(path);
	}

    // キャッシュを保存して返す
    m_Shaders.emplace(key, shader);
    return shader.get();
}

const Shader* ShaderLibrary::Reload(const std::wstring& filePath, const std::string& entry, const std::string& profile, std::string& outError, UINT flags)
{
    const std::wstring path = ResolveShaderPath(filePath);
    const ShaderKey key{ path,entry,profile,flags };
    auto shader = std::make_shared<Shader>();
    if(!shader->LoadFromFile(path, entry, profile, flags))
    {
        outError = shader->GetLastError(); // UIへ戻す
        return nullptr;
	}
    outError.clear();
    ShaderEntry e{ shader };
    if (std::filesystem::exists(path))
    {
        e.lastWriteTime = std::filesystem::last_write_time(path);        
    }
    m_Shaders[key] = e; // 上書き
    return shader.get();
}

void ShaderLibrary::Clear()
{
    m_Shaders.clear();
}

bool ShaderLibrary::ReoadChanged()
{
    bool changed = false;

    for (auto& [key, entry] : m_Shaders)
    {
        // ファイルの最終更新日時を取得
        if (!std::filesystem::exists(key.filePath))
        {
            // ファイルが存在しない場合はスキップ
            continue;
        }

		const auto current = std::filesystem::last_write_time(key.filePath);
        if (current == entry.lastWriteTime)
        {
            // 更新されていない場合はスキップ
			continue;
        }

        auto shader = std::make_shared<Shader>();

        // シェーダーが取得できない場合はスキップ
        if (!shader->LoadFromFile(
            key.filePath,
            key.entryPoint,
            key.profile,
            key.compileFlags))
        {
            continue;
        }

		entry.shader = shader;
        entry.lastWriteTime = current;
		changed = true;
    }
	return changed;
}
