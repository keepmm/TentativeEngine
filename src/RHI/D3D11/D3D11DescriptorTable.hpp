/*****************************************************************//**
 * \file   D3D11DescriptorTable.hpp
 * \brief  IRHIDescriptorTable の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include "D3D11Types.hpp"
#include "D3D11Resource.hpp"
#include "../IRHIDescriptorTable.hpp"

class D3D11DescriptorTable final : public IRHIDescriptorTable
{
public:
	D3D11DescriptorTable(_In_ uint32_t startRegister, _In_ uint32_t count);
	~D3D11DescriptorTable() override = default;

	// ---- IRHIDescriptorTable ---- //
	uint32_t GetCount() const noexcept override { return m_Count; }
	void SetTexture(_In_ uint32_t index, _In_ const RHITextureRef& texture) override;
	void ClearTexture(_In_ uint32_t index) override;

	// ---- D3D11 固有アクセサ ---- //
	uint32_t GetStartRegister() const noexcept { return m_StartRegister; }
	ID3D11ShaderResourceView* const* GetSrvArray() const noexcept { return m_Srvs.data(); }

private:
	uint32_t m_StartRegister = 0;
	uint32_t m_Count = 0;
	std::vector<RHITextureRef> m_Textures;
	std::vector<ID3D11ShaderResourceView*> m_Srvs;
};
