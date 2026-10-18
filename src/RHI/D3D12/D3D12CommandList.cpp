/*****************************************************************//**
 * \file   D3D12CommandList.cpp
 * \brief  IRHICommandList の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成（バリア / レンダーパス / コピー / PSO / 描画）
 * *********************************************************************/
#include "D3D12CommandList.hpp"
#include "D3D12DescriptorTable.hpp"
#include "D3D12Resource.hpp"
#include "ThirdParty/d3dx12.h"

#include <algorithm>

namespace
{
	/// @brief RHI のコピー配置を、D3D12 のコピー位置（バッファ側）にする
	D3D12_TEXTURE_COPY_LOCATION MakeBufferLocation(
		ID3D12Resource* buffer, ID3D12Resource* texture, const RHISubresourceFootprint& fp)
	{
		DXGI_FORMAT format = texture->GetDesc().Format;
		if (format == DXGI_FORMAT_D32_FLOAT)
		{
			format = DXGI_FORMAT_R32_TYPELESS;	// 深度はコピー用の形式で並べる
		}

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
		placed.Offset				= fp.offset;
		placed.Footprint.Format		= format;
		placed.Footprint.Width		= fp.width;
		placed.Footprint.Height		= fp.height;
		placed.Footprint.Depth		= fp.depth;
		placed.Footprint.RowPitch	= fp.rowPitch;
		return CD3DX12_TEXTURE_COPY_LOCATION(buffer, placed);
	}
}

D3D12CommandList::D3D12CommandList(
	ComPtr<ID3D12CommandAllocator> allocator,
	ComPtr<ID3D12GraphicsCommandList> list,
	ID3D12DescriptorHeap* shaderVisibleHeap)
	: m_Allocator(std::move(allocator))
	, m_List(std::move(list))
	, m_ShaderVisibleHeap(shaderVisibleHeap)
{
	m_PendingBarriers.reserve(16);
}

// ---------------------------------------------------//
//					記録の開始 / 終了					  //
// ---------------------------------------------------//
void D3D12CommandList::Begin()
{
	assert(!m_Recording && "Begin が二重に呼ばれた（End を忘れていないか）");

	// GPU がこのアロケータの中身を使い終わっていないと失敗する（デバッグレイヤーが止める）
	m_Allocator->Reset();
	m_List->Reset(m_Allocator.Get(), nullptr);

	// デスクリプタテーブルはすべてデバイスのシェーダー可視ヒープにあるので、最初に 1 回設定しておく
	ID3D12DescriptorHeap* heaps[] = { m_ShaderVisibleHeap };
	m_List->SetDescriptorHeaps(1, heaps);

	m_Recording		= true;
	m_InRenderPass	= false;

	// 前の記録で設定したものは Reset で消えているので、こちらの控えも消す
	m_Pipeline				= nullptr;
	m_RootSignature			= nullptr;
	m_VertexBufferCount		= 0;
	m_VertexBuffersDirty	= false;
	for (VertexBufferBinding& vb : m_VertexBuffers)
	{
		vb = {};
	}
}

void D3D12CommandList::End()
{
	assert(m_Recording && "Begin していない");
	assert(!m_InRenderPass && "EndRenderPass を忘れている");

	FlushBarriers();
	m_List->Close();
	m_Recording = false;
}

// ---------------------------------------------------//
//						バリア						  //
// ---------------------------------------------------//
void D3D12CommandList::Transition(IRHITexture* texture, ERHIResourceState before, ERHIResourceState after)
{
	AddTransition(static_cast<D3D12Texture*>(texture)->GetResource(), before, after);
}

void D3D12CommandList::Transition(IRHIBuffer* buffer, ERHIResourceState before, ERHIResourceState after)
{
	AddTransition(static_cast<D3D12Buffer*>(buffer)->GetResource(), before, after);
}

void D3D12CommandList::AddTransition(ID3D12Resource* resource, ERHIResourceState before, ERHIResourceState after)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではバリアを張れない（Vulkan の制約）");

	const D3D12_RESOURCE_STATES d3dBefore	= ToD3D12ResourceStates(before);
	const D3D12_RESOURCE_STATES d3dAfter	= ToD3D12ResourceStates(after);

	if (d3dBefore == d3dAfter)
	{
		// UAV → UAV は「前の書き込みを待つ」UAV バリアにする
		if (before == ERHIResourceState::UnorderedAccess && after == ERHIResourceState::UnorderedAccess)
		{
			m_PendingBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(resource));
		}
		// それ以外（Undefined → Present など、D3D12 では同じ値になるもの）は出さない。
		// 同じ状態への遷移はデバッグレイヤーがエラーにする
		return;
	}
	m_PendingBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(resource, d3dBefore, d3dAfter));
}

void D3D12CommandList::FlushBarriers()
{
	if (m_PendingBarriers.empty()) return;

	m_List->ResourceBarrier(static_cast<UINT>(m_PendingBarriers.size()), m_PendingBarriers.data());
	m_PendingBarriers.clear();
}

// ---------------------------------------------------//
//					レンダーパス						  //
// ---------------------------------------------------//
void D3D12CommandList::BeginRenderPass(const RHIRenderPassDesc& desc)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "BeginRenderPass が入れ子になっている");
	assert(desc.numColors <= RHI_MAX_RENDER_TARGETS);

	FlushBarriers();

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[RHI_MAX_RENDER_TARGETS]{};
	for (uint32_t i = 0; i < desc.numColors; ++i)
	{
		rtvs[i] = static_cast<D3D12Texture*>(desc.colors[i].texture)->GetRtv();
		assert(rtvs[i].ptr != 0 && "RenderTarget 用途で作っていないテクスチャ");
	}

	D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
	const bool hasDepth = desc.depth.texture != nullptr;
	if (hasDepth)
	{
		dsv = static_cast<D3D12Texture*>(desc.depth.texture)->GetDsv();
		assert(dsv.ptr != 0 && "DepthStencil 用途で作っていないテクスチャ");
	}

	m_List->OMSetRenderTargets(desc.numColors, rtvs, FALSE, hasDepth ? &dsv : nullptr);

	// loadOp = Clear のものだけ塗る。Load / DontCare は D3D12 では何もしない
	for (uint32_t i = 0; i < desc.numColors; ++i)
	{
		if (desc.colors[i].loadOp == ERHILoadOp::Clear)
		{
			m_List->ClearRenderTargetView(rtvs[i], desc.colors[i].clearColor, 0, nullptr);
		}
	}
	if (hasDepth && desc.depth.loadOp == ERHILoadOp::Clear)
	{
		m_List->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
			desc.depth.clearDepth, desc.depth.clearStencil, 0, nullptr);
	}

	m_InRenderPass = true;
}

void D3D12CommandList::EndRenderPass()
{
	assert(m_InRenderPass && "BeginRenderPass していない");

	// D3D12 では何もしなくてよい。Vulkan ではここで vkCmdEndRendering
	m_InRenderPass = false;
}

void D3D12CommandList::SetViewport(const RHIViewport& viewport)
{
	const D3D12_VIEWPORT vp = ToD3D12Viewport(viewport);
	m_List->RSSetViewports(1, &vp);
}

void D3D12CommandList::SetScissor(const RHIRect& rect)
{
	const D3D12_RECT r = ToD3D12Rect(rect);
	m_List->RSSetScissorRects(1, &r);
}

// ---------------------------------------------------//
//				パイプライン / バインド					  //
// ---------------------------------------------------//
void D3D12CommandList::SetPipelineState(IRHIPipelineState* pipeline)
{
	assert(m_Recording);
	auto* pso = static_cast<D3D12PipelineState*>(pipeline);

	// ルートシグネチャは変わるときだけ設定し直す（変えるとそれまでのルート引数が消える）
	ID3D12RootSignature* rootSignature = pso->GetRootSignature();
	if (rootSignature != m_RootSignature)
	{
		m_List->SetGraphicsRootSignature(rootSignature);
		m_RootSignature = rootSignature;
	}

	m_List->SetPipelineState(pso->GetPipelineState());

	// トポロジーは PSO が持っている（Vulkan ではパイプラインに焼き込まれるため）
	m_List->IASetPrimitiveTopology(pso->GetTopology());

	m_Pipeline				= pso;
	m_VertexBuffersDirty	= true;	// ストライドが変わるかもしれないので組み直す
}

void D3D12CommandList::CheckSlot(uint32_t slot, ERHIBindingType type) const
{
	assert(m_Pipeline != nullptr && "SetPipelineState より前にバインドしている");
	assert(slot < m_Pipeline->GetLayout()->GetSlotCount() && "レイアウトに無いスロット");
	assert(m_Pipeline->GetLayout()->GetSlotType(slot) == type && "スロットの種類が違う");
	(void)slot;
	(void)type;
}

void D3D12CommandList::SetConstantBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
	CheckSlot(slot, ERHIBindingType::ConstantBuffer);
	assert(offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0 && "定数バッファのオフセットは 256 の倍数");

	m_List->SetGraphicsRootConstantBufferView(slot,
		static_cast<D3D12Buffer*>(buffer)->GetGpuAddress() + offset);
}

void D3D12CommandList::SetStructuredBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
	CheckSlot(slot, ERHIBindingType::StructuredBuffer);

	m_List->SetGraphicsRootShaderResourceView(slot,
		static_cast<D3D12Buffer*>(buffer)->GetGpuAddress() + offset);
}

void D3D12CommandList::SetVertexBuffer(uint32_t bufferSlot, IRHIBuffer* buffer, uint64_t offset)
{
	assert(bufferSlot < MAX_VERTEX_BUFFERS);

	// D3D12 はここでストライドが要るが、RHI では PSO が持っているので Draw の直前に組む
	m_VertexBuffers[bufferSlot]	= { buffer, offset };
	m_VertexBufferCount			= (std::max)(m_VertexBufferCount, bufferSlot + 1);
	m_VertexBuffersDirty		= true;
}

void D3D12CommandList::FlushVertexBuffers()
{
	if (!m_VertexBuffersDirty || m_VertexBufferCount == 0) return;

	D3D12_VERTEX_BUFFER_VIEW views[MAX_VERTEX_BUFFERS]{};
	for (uint32_t i = 0; i < m_VertexBufferCount; ++i)
	{
		const VertexBufferBinding& binding = m_VertexBuffers[i];
		if (binding.buffer == nullptr) continue;	// 未設定のスロットは空のまま

		auto* buffer = static_cast<D3D12Buffer*>(binding.buffer);
		views[i].BufferLocation	= buffer->GetGpuAddress() + binding.offset;
		views[i].SizeInBytes	= static_cast<UINT>(buffer->GetDesc().size - binding.offset);
		views[i].StrideInBytes	= m_Pipeline->GetVertexStride(i);
	}
	m_List->IASetVertexBuffers(0, m_VertexBufferCount, views);
	m_VertexBuffersDirty = false;
}

void D3D12CommandList::SetIndexBuffer(IRHIBuffer* buffer, ERHIFormat format, uint64_t offset)
{
	assert(format == ERHIFormat::R16_UINT || format == ERHIFormat::R32_UINT);

	auto* b = static_cast<D3D12Buffer*>(buffer);
	D3D12_INDEX_BUFFER_VIEW view{};
	view.BufferLocation	= b->GetGpuAddress() + offset;
	view.SizeInBytes	= static_cast<UINT>(b->GetDesc().size - offset);
	view.Format			= ToDXGIFormat(format);
	m_List->IASetIndexBuffer(&view);
}

void D3D12CommandList::SetDescriptorTable(uint32_t slot, IRHIDescriptorTable* table)
{
	CheckSlot(slot, ERHIBindingType::TextureTable);
	assert(table->GetCount() == m_Pipeline->GetLayout()->GetSlot(slot).count && "テーブルの枚数がスロットと違う");

	m_List->SetGraphicsRootDescriptorTable(slot,
		static_cast<D3D12DescriptorTable*>(table)->GetGpuHandle());
}

// ---------------------------------------------------//
//						描画							  //
// ---------------------------------------------------//
void D3D12CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	assert(m_InRenderPass && "Draw はレンダーパスの中で");
	assert(m_Pipeline != nullptr && "SetPipelineState していない");

	FlushVertexBuffers();
	m_List->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void D3D12CommandList::DrawIndexed(
	uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstIndex, int32_t baseVertex,
	uint32_t firstInstance)
{
	assert(m_InRenderPass && "DrawIndexed はレンダーパスの中で");
	assert(m_Pipeline != nullptr && "SetPipelineState していない");

	FlushVertexBuffers();
	m_List->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

// ---------------------------------------------------//
//						コピー						  //
// ---------------------------------------------------//
void D3D12CommandList::CopyBuffer(
	IRHIBuffer* dst, uint64_t dstOffset,
	IRHIBuffer* src, uint64_t srcOffset,
	uint64_t size)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではコピーできない（Vulkan の制約）");
	assert(dstOffset + size <= dst->GetDesc().size && srcOffset + size <= src->GetDesc().size);

	FlushBarriers();
	m_List->CopyBufferRegion(
		static_cast<D3D12Buffer*>(dst)->GetResource(), dstOffset,
		static_cast<D3D12Buffer*>(src)->GetResource(), srcOffset,
		size);
}

void D3D12CommandList::CopyBufferToTexture(
	IRHITexture* dst, uint32_t dstSubresource,
	IRHIBuffer* src, const RHISubresourceFootprint& footprint)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではコピーできない（Vulkan の制約）");

	FlushBarriers();
	ID3D12Resource* texture = static_cast<D3D12Texture*>(dst)->GetResource();
	const CD3DX12_TEXTURE_COPY_LOCATION dstLocation(texture, dstSubresource);
	const D3D12_TEXTURE_COPY_LOCATION srcLocation =
		MakeBufferLocation(static_cast<D3D12Buffer*>(src)->GetResource(), texture, footprint);
	m_List->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
}

void D3D12CommandList::CopyTextureToBuffer(
	IRHIBuffer* dst, const RHISubresourceFootprint& footprint,
	IRHITexture* src, uint32_t srcSubresource)
{
	assert(m_Recording);
	assert(!m_InRenderPass && "レンダーパスの中ではコピーできない（Vulkan の制約）");

	FlushBarriers();
	ID3D12Resource* texture = static_cast<D3D12Texture*>(src)->GetResource();
	const D3D12_TEXTURE_COPY_LOCATION dstLocation =
		MakeBufferLocation(static_cast<D3D12Buffer*>(dst)->GetResource(), texture, footprint);
	const CD3DX12_TEXTURE_COPY_LOCATION srcLocation(texture, srcSubresource);
	m_List->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
}
