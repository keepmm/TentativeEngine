/*****************************************************************//**
 * \file   ShaderLibrary.hpp
 * \brief  Shaderキャッシュ管理
 * 
 * 作成者 keeep
 * 作成日 2026/5/8
 * 更新履歴	5.8 作成
 *			5.8 ホットリロード対応
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include "Shader.hpp"

struct ShaderKey
{
	std::wstring filePath;
	std::string entryPoint;
	std::string profile;
	UINT compileFlags = 0;

	/// @brief ShaderKeyの比較演算子
	/// @param other 比較対象のShaderKey
	/// @return すべてのメンバが等しい場合はtrue、それ以外はfalse
	bool operator==(const ShaderKey& other) const
	{
		return filePath == other.filePath &&
			entryPoint == other.entryPoint &&
			profile == other.profile &&
			compileFlags == other.compileFlags;
	}
};

struct ShaderKeyHash
{
	size_t operator()(const ShaderKey& key) const
	{
		const size_t h1 = std::hash<std::wstring>()(key.filePath);
		const size_t h2 = std::hash<std::string>()(key.entryPoint);
		const size_t h3 = std::hash<std::string>()(key.profile);
		const size_t h4 = std::hash<UINT>()(key.compileFlags);

		return (((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1)) ^ (h4 << 2);
	}
};

struct ShaderEntry
{
	std::shared_ptr<Shader> shader;
	std::filesystem::file_time_type lastWriteTime{};
};

class ShaderLibrary
{
public:
	const Shader* Load(
		_In_ const std::wstring& filePath,
		_In_ const std::string& entryPoint,
		_In_ const std::string& profile,
		UINT compileFlags = 0
		);

	const Shader* Reload(
		_In_ const std::wstring& filePath,
		_In_ const std::string& entry,
		_In_ const std::string& profile,
		_Out_ std::string& outError,
		_In_ UINT flags = 0
	);

	void Clear();

	bool ReoadChanged();
private:
	std::unordered_map<ShaderKey, ShaderEntry, ShaderKeyHash> m_Shaders;
};

