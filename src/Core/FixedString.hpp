/*****************************************************************//**
 * \file   FixedString.hpp
 * \brief  固定長の文字列（ヒープを使わない）
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#pragma once

#include <cstddef>

namespace TE
{
	/// @brief 終端の 0 込みで N バイトの文字列。中身は UTF-8
	/// @note  ヒープを指さないので、コンポーネントに入れてもチャンクの中に文字がそのまま並び、
	///        memcpy でコピーできる（シーンのスナップショット）。ImGui の InputText にもそのまま渡せる。
	///        入りきらない分は切り捨てる（UTF-8 の文字の途中で切れることはある）
	template <std::size_t N>
	struct FixedString
	{
		static_assert(N > 1, "終端の 0 を入れる分が要る");

		char data[N] = {};

		constexpr FixedString() = default;
		// FixedString<64> name = "Spot"; と書けるよう、暗黙の変換を許す
		constexpr FixedString(const char* text) { Assign(text); }

		constexpr void Assign(const char* text)
		{
			std::size_t i = 0;
			for (; text != nullptr && text[i] != '\0' && i < N - 1; ++i)
			{
				data[i] = text[i];
			}
			for (; i < N; ++i)
			{
				data[i] = '\0';
			}
		}

		constexpr const char* CStr() const { return data; }
		constexpr bool Empty() const { return data[0] == '\0'; }
		static constexpr std::size_t Capacity() { return N; }
	};
}
