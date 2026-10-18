/*****************************************************************//**
 * \file   RHIUtil.hpp
 * \brief  RHI のインターフェースだけで書ける便利関数（バックエンドに依存しない）
 *
 * 作成者 keepmm
 * 作成日 2026/10/15
 * 更新履歴 10.15 作成（テクスチャのアップロード）
 * *********************************************************************/
#pragma once

#include "RHITypes.hpp"

/// @brief CPU の画像データをテクスチャへ送るコマンドを記録する
/// @param firstSubresource subresources[0] を書き込むサブリソース番号（RHISubresourceIndex で求める）
/// @param count            subresources の個数
/// @return 使ったステージング（Upload）バッファ。失敗時は nullptr。
///         持っておく必要はない。手放しても、GPU が使い終わるまでは遅延解放で残る
/// @note  texture は CopyDst 状態にしておくこと。コピー後の遷移も呼び出し側で記録する
RHIBufferRef RHIUploadTexture(
	_In_ IRHIDevice& device,
	_In_ IRHICommandList& commandList,
	_In_ IRHITexture* texture,
	_In_ uint32_t firstSubresource,
	_In_ uint32_t count,
	_In_reads_(count) const RHISubresourceData* subresources);
