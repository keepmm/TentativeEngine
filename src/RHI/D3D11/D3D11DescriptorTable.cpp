/*****************************************************************//**
 * \file   D3D11DescriptorTable.cpp
 * \brief  IRHIDescriptorTable の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#include "D3D11DescriptorTable.hpp"

D3D11DescriptorTable::D3D11DescriptorTable(uint32_t startRegister, uint32_t count)
	: m_StartRegister(startRegister)
	, m_Count(count)
	, m_Textures(count)
	, m_Srvs(count, nullptr)
{
}

void D3D11DescriptorTable::SetTexture(uint32_t index, const RHITextureRef& texture)
{
	if (index >= m_Count) return;

	m_Textures[index] = texture;
	m_Srvs[index] = texture ? static_cast<D3D11Texture*>(texture.get())->GetSrv() : nullptr;
}

void D3D11DescriptorTable::ClearTexture(uint32_t index)
{
	if (index >= m_Count) return;

	m_Textures[index].reset();
	m_Srvs[index] = nullptr;
}
