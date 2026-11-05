/*****************************************************************//**
 * \file   Name.hpp
 * \brief  Entity の名前（エディターの Hierarchy に出す）
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#pragma once

#include "Core/FixedString.hpp"

namespace TE
{
	/// @brief 64 バイト = キャッシュライン 1 本。名前で探す処理は持たない（表示用）
	struct Name
	{
		FixedString<64> value;
	};
}
