/*****************************************************************//**
 * \file   D3D12SwapChain.hpp
 * \brief  IRHISwapChain の D3D12 実装
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "D3D12Resource.hpp"
#include "../IRHISwapChain.hpp"

class D3D12Device;

class D3D12SwapChain final : public IRHISwapChain
{
public:
	explicit D3D12SwapChain(_In_ D3D12Device& device);
	~D3D12SwapChain() override;

	/// @brief スワップチェインを作る
	/// @return 失敗時 false
	bool Init(_In_ const RHISwapChainDesc& desc);

	// ---- IRHISwapChain ---- //
	uint32_t AcquireNextImage() override;
	IRHITexture* GetBackBuffer(_In_ uint32_t index) const override;
	ERHIPresentResult Present() override;
	bool Resize(_In_ uint32_t width, _In_ uint32_t height) override;

	uint32_t GetBufferCount() const noexcept override { return m_BufferCount; }
	uint32_t GetWidth() const noexcept override { return m_Width; }
	uint32_t GetHeight() const noexcept override { return m_Height; }
	ERHIFormat GetFormat() const noexcept override { return m_Format; }
	void* GetNativeSwapChain() const noexcept override { return m_SwapChain.Get(); }

private:
	/// @brief バックバッファを D3D12Texture で包む（RTV もここで作られる）
	bool CreateBackBuffers();

	D3D12Device&			m_Device;
	ComPtr<IDXGISwapChain3>	m_SwapChain;

	// 持ち主はスワップチェイン。Resize のときにすぐ手放す必要があるので遅延解放は通さない
	std::vector<std::unique_ptr<D3D12Texture>> m_BackBuffers;

	uint32_t	m_BufferCount	= 0;
	uint32_t	m_Width			= 0;
	uint32_t	m_Height		= 0;
	ERHIFormat	m_Format		= ERHIFormat::Unknown;
	bool		m_VSync			= true;
	bool		m_AllowTearing	= false;	// VSync なしで本当に同期を外すのに必要
};
