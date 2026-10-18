/*****************************************************************//**
 * \file   D3D11CommandList.cpp
 * \brief  IRHICommandList の DirectX 11 実装
 * 
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#include "D3D11CommandList.hpp"

D3D11CommandList::D3D11CommandList(ComPtr<ID3D11DeviceContext> context)
	: m_Context(std::move(context))
{
}

void D3D11CommandList::Begin()
{
	m_CurrentPso = nullptr;
}

void D3D11CommandList::End()
{
}

void D3D11CommandList::Transition(IRHITexture* texture, ERHIResourceState before, ERHIResourceState after)
{
	// D3D11 はランタイムが自動的にリソースステートを管理するため不要
	(void)texture; (void)before; (void)after;
}

void D3D11CommandList::Transition(IRHIBuffer* buffer, ERHIResourceState before, ERHIResourceState after)
{
	(void)buffer; (void)before; (void)after;
}

void D3D11CommandList::BeginRenderPass(const RHIRenderPassDesc& desc)
{
	if (!m_Context) return;

	ID3D11RenderTargetView* rtvs[RHI_MAX_RENDER_TARGETS]{};
	for (uint32_t i = 0; i < desc.numColors && i < RHI_MAX_RENDER_TARGETS; ++i)
	{
		if (desc.colors[i].texture)
		{
			auto* tex = static_cast<D3D11Texture*>(desc.colors[i].texture);
			rtvs[i] = tex->GetRtv();
			if (rtvs[i] && desc.colors[i].loadOp == ERHILoadOp::Clear)
			{
				m_Context->ClearRenderTargetView(rtvs[i], desc.colors[i].clearColor);
			}
		}
	}

	ID3D11DepthStencilView* dsv = nullptr;
	if (desc.depth.texture)
	{
		auto* tex = static_cast<D3D11Texture*>(desc.depth.texture);
		dsv = tex->GetDsv();
		if (dsv && desc.depth.loadOp == ERHILoadOp::Clear)
		{
			m_Context->ClearDepthStencilView(
				dsv,
				D3D11_CLEAR_DEPTH,
				desc.depth.clearDepth,
				desc.depth.clearStencil);
		}
	}

	m_Context->OMSetRenderTargets(desc.numColors, rtvs, dsv);
}

void D3D11CommandList::EndRenderPass()
{
	if (!m_Context) return;
	ID3D11RenderTargetView* nullRtvs[RHI_MAX_RENDER_TARGETS]{};
	m_Context->OMSetRenderTargets(RHI_MAX_RENDER_TARGETS, nullRtvs, nullptr);
}

void D3D11CommandList::SetViewport(const RHIViewport& viewport)
{
	if (!m_Context) return;
	D3D11_VIEWPORT d3dvp{};
	d3dvp.TopLeftX = viewport.x;
	d3dvp.TopLeftY = viewport.y;
	d3dvp.Width = viewport.width;
	d3dvp.Height = viewport.height;
	d3dvp.MinDepth = viewport.minDepth;
	d3dvp.MaxDepth = viewport.maxDepth;
	m_Context->RSSetViewports(1, &d3dvp);
}

void D3D11CommandList::SetScissor(const RHIRect& rect)
{
	if (!m_Context) return;
	D3D11_RECT d3drc{};
	d3drc.left = rect.left;
	d3drc.top = rect.top;
	d3drc.right = rect.right;
	d3drc.bottom = rect.bottom;
	m_Context->RSSetScissorRects(1, &d3drc);
}

void D3D11CommandList::CopyBuffer(
	IRHIBuffer* dst, uint64_t dstOffset,
	IRHIBuffer* src, uint64_t srcOffset,
	uint64_t size)
{
	if (!dst || !src || !m_Context) return;

	auto* d3dDst = static_cast<D3D11Buffer*>(dst);
	auto* d3dSrc = static_cast<D3D11Buffer*>(src);

	D3D11_BOX box{};
	box.left = static_cast<UINT>(srcOffset);
	box.right = static_cast<UINT>(srcOffset + size);
	box.top = 0; box.bottom = 1;
	box.front = 0; box.back = 1;

	m_Context->CopySubresourceRegion(
		d3dDst->GetBuffer(), 0, static_cast<UINT>(dstOffset), 0, 0,
		d3dSrc->GetBuffer(), 0, &box);
}

void D3D11CommandList::CopyBufferToTexture(
	IRHITexture* dst, uint32_t dstSubresource,
	IRHIBuffer* src, const RHISubresourceFootprint& footprint)
{
	if (!dst || !src || !m_Context) return;

	auto* d3dDst = static_cast<D3D11Texture*>(dst);
	auto* d3dSrc = static_cast<D3D11Buffer*>(src);

	const uint8_t* pSrc = d3dSrc->GetCpuData();
	if (!pSrc) return;

	pSrc += footprint.offset;

	UINT srcRowPitch = footprint.rowPitch;
	UINT srcDepthPitch = footprint.depth > 1 ? (footprint.rowPitch * footprint.height) : 0;

	// サブリソース全体を一括更新するため pDstBox には nullptr を指定
	m_Context->UpdateSubresource(
		d3dDst->GetTexture2D(),
		dstSubresource,
		nullptr,
		pSrc,
		srcRowPitch,
		srcDepthPitch);
}

void D3D11CommandList::CopyTextureToBuffer(
	IRHIBuffer* dst, const RHISubresourceFootprint& footprint,
	IRHITexture* src, uint32_t srcSubresource)
{
	(void)dst; (void)footprint; (void)src; (void)srcSubresource;
}

void D3D11CommandList::SetPipelineState(IRHIPipelineState* pipeline)
{
	m_CurrentPso = static_cast<D3D11PipelineState*>(pipeline);
	if (m_CurrentPso && m_Context)
	{
		m_CurrentPso->Apply(m_Context.Get());
	}
}

void D3D11CommandList::SetConstantBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
	(void)offset;
	if (!m_Context || !buffer) return;

	auto* b = static_cast<D3D11Buffer*>(buffer);
	b->Flush(m_Context.Get());

	ID3D11Buffer* d3db = b->GetBuffer();
	m_Context->VSSetConstantBuffers(slot, 1, &d3db);
	m_Context->PSSetConstantBuffers(slot, 1, &d3db);
}

void D3D11CommandList::SetStructuredBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
	(void)offset;
	if (!m_Context || !buffer) return;

	auto* b = static_cast<D3D11Buffer*>(buffer);
	b->Flush(m_Context.Get());

	ID3D11ShaderResourceView* srv = b->GetSrv();
	if (srv)
	{
		m_Context->VSSetShaderResources(slot, 1, &srv);
		m_Context->PSSetShaderResources(slot, 1, &srv);
	}
}

void D3D11CommandList::SetVertexBuffer(uint32_t bufferSlot, IRHIBuffer* buffer, uint64_t offset)
{
	if (!m_Context || !buffer) return;

	auto* b = static_cast<D3D11Buffer*>(buffer);
	b->Flush(m_Context.Get());

	ID3D11Buffer* d3db = b->GetBuffer();
	UINT stride = m_CurrentPso ? m_CurrentPso->GetVertexBufferStride(bufferSlot) : 0;
	UINT off = static_cast<UINT>(offset);

	m_Context->IASetVertexBuffers(bufferSlot, 1, &d3db, &stride, &off);
}

void D3D11CommandList::SetIndexBuffer(IRHIBuffer* buffer, ERHIFormat format, uint64_t offset)
{
	if (!m_Context || !buffer) return;

	auto* b = static_cast<D3D11Buffer*>(buffer);
	b->Flush(m_Context.Get());

	ID3D11Buffer* d3db = b->GetBuffer();
	m_Context->IASetIndexBuffer(d3db, ToD3D11Format(format), static_cast<UINT>(offset));
}

void D3D11CommandList::SetDescriptorTable(uint32_t slot, IRHIDescriptorTable* table)
{
	(void)slot;
	if (!m_Context || !table) return;

	auto* dt = static_cast<D3D11DescriptorTable*>(table);
	m_Context->PSSetShaderResources(dt->GetStartRegister(), dt->GetCount(), dt->GetSrvArray());
}

void D3D11CommandList::Draw(
	uint32_t vertexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstInstance)
{
	if (!m_Context) return;
	if (instanceCount > 1)
	{
		m_Context->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
	}
	else
	{
		m_Context->Draw(vertexCount, firstVertex);
	}
}

void D3D11CommandList::DrawIndexed(
	uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstIndex, int32_t baseVertex,
	uint32_t firstInstance)
{
	if (!m_Context) return;
	if (instanceCount > 1)
	{
		m_Context->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
	}
	else
	{
		m_Context->DrawIndexed(indexCount, firstIndex, baseVertex);
	}
}
