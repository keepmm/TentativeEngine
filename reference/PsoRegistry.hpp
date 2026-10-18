#pragma once

#include "Defines.hpp"

class PsoRegistry
{
public:
	void Register(_In_ const std::string& name, _In_ ComPtr<ID3D12PipelineState> pso)
	{
		m_Table[name] = std::move(pso);
	}

	ID3D12PipelineState* Get(_In_ const std::string& name) const
	{
		auto it = m_Table.find(name);
		return it != m_Table.end() ? it->second.Get() : nullptr;
	}

	bool Contains(_In_ const std::string& name) const
	{
		return m_Table.contains(name);
	}

	void Clear() { m_Table.clear(); }

private:
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_Table;
};

