/*****************************************************************//**
 * \file   D3D11Resource.hpp
 * \brief  IRHITexture / IRHIBuffer の DirectX 11 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/17
 * *********************************************************************/
#pragma once

#include "D3D11Types.hpp"
#include "../IRHITexture.hpp"
#include "../IRHIBuffer.hpp"

class D3D11Texture final : public IRHITexture
{
public:
	D3D11Texture(
		_In_ const RHITextureDesc& desc,
		_In_ ComPtr<ID3D11Texture2D> texture);
	~D3D11Texture() override = default;

	/// @brief SRV, RTV, DSV を usage に応じて作成する
	bool CreateViews(_In_ ID3D11Device* device);

	const RHITextureDesc& GetDesc() const noexcept override { return m_Desc; }
	void* GetNativeResource() const noexcept override { return m_Texture.Get(); }

	ID3D11Texture2D* GetTexture2D() const noexcept { return m_Texture.Get(); }
	ID3D11ShaderResourceView* GetSrv() const noexcept { return m_Srv.Get(); }
	ID3D11RenderTargetView* GetRtv() const noexcept { return m_Rtv.Get(); }
	ID3D11DepthStencilView* GetDsv() const noexcept { return m_Dsv.Get(); }

private:
	RHITextureDesc m_Desc;
	ComPtr<ID3D11Texture2D> m_Texture;
	ComPtr<ID3D11ShaderResourceView> m_Srv;
	ComPtr<ID3D11RenderTargetView> m_Rtv;
	ComPtr<ID3D11DepthStencilView> m_Dsv;
};

class D3D11Buffer final : public IRHIBuffer
{
public:
	D3D11Buffer(
		_In_ const RHIBufferDesc& desc,
		_In_ ComPtr<ID3D11Buffer> buffer,
		_In_opt_ ComPtr<ID3D11ShaderResourceView> srv = nullptr);
	~D3D11Buffer() override = default;

	const RHIBufferDesc& GetDesc() const noexcept override { return m_Desc; }
	void* GetNativeResource() const noexcept override { return m_Buffer.Get(); }

	ID3D11Buffer* GetBuffer() const noexcept { return m_Buffer.Get(); }
	ID3D11ShaderResourceView* GetSrv() const noexcept { return m_Srv.Get(); }

	void* Map() override;
	void Unmap() override;

	/// @brief CPU シャドウバッファの内容を D3D11 の動的バッファへ転送する
	void Flush(_In_ ID3D11DeviceContext* context);

	/// @brief 初期データを CPU シャドウバッファに書き込む（Map/Unmap フラグは操作しない）
	void SetInitialData(_In_ const void* data, _In_ size_t size);

	/// @brief テクスチャアップロード等で CPU データを直接参照する用
	const uint8_t* GetCpuData() const noexcept { return m_CpuData.empty() ? nullptr : m_CpuData.data(); }

private:
	RHIBufferDesc m_Desc;
	ComPtr<ID3D11Buffer> m_Buffer;
	ComPtr<ID3D11ShaderResourceView> m_Srv;
	std::vector<uint8_t> m_CpuData;
	bool m_IsDirty = false;
	bool m_IsMapped = false;
};