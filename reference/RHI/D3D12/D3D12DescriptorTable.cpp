/*****************************************************************//**
 * \file   D3D12DescriptorTable.cpp
 * \brief  IRHIDescriptorTable の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#include "D3D12DescriptorTable.hpp"
#include "D3D12Device.hpp"
#include "D3D12Resource.hpp"

D3D12DescriptorTable::D3D12DescriptorTable(
	D3D12Device& device,
	uint32_t start,
	uint32_t count)
	: m_Device(device)
	, m_Start(start)
	, m_Count(count)
	, m_Textures(count)
{
	// 中身が不定のまま描画に使われないよう、最初は全部空にしておく
	for (uint32_t i = 0; i < m_Count; ++i)
	{
		ClearTexture(i);
	}
}

D3D12DescriptorTable::~D3D12DescriptorTable()
{
	// 遅延解放を経てここに来る（= GPU は使い終わっている）ので範囲を返してよい
	m_Device.GetShaderVisibleHeap().FreeRange(m_Start, m_Count);
}

void D3D12DescriptorTable::SetTexture(uint32_t index, const RHITextureRef& texture)
{
	assert(index < m_Count);

	auto* d3dTexture = static_cast<D3D12Texture*>(texture.get());
	if (d3dTexture == nullptr || d3dTexture->GetSrv().ptr == 0)
	{
		assert(!"SetTexture: ShaderResource 用途で作っていないテクスチャ");
		ClearTexture(index);
		return;
	}

	// SRV の実体は CPU 専用ヒープにある。シェーダーから見える場所へ写す
	m_Device.GetD3D12Device()->CopyDescriptorsSimple(1,
		m_Device.GetShaderVisibleHeap().Cpu(m_Start + index),
		d3dTexture->GetSrv(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_Textures[index] = texture;
}

void D3D12DescriptorTable::ClearTexture(uint32_t index)
{
	assert(index < m_Count);

	// null SRV（読むと 0 が返る）を置く
	m_Device.GetD3D12Device()->CopyDescriptorsSimple(1,
		m_Device.GetShaderVisibleHeap().Cpu(m_Start + index),
		m_Device.GetNullSrv(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_Textures[index].reset();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorTable::GetGpuHandle() const noexcept
{
	return m_Device.GetShaderVisibleHeap().Gpu(m_Start);
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorTable::GetGpuHandle(uint32_t index) const noexcept
{
	assert(index < m_Count);
	return m_Device.GetShaderVisibleHeap().Gpu(m_Start + index);
}
