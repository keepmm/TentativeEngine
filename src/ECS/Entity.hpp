/*****************************************************************//**
 * \file   Entity.hpp
 * \brief  Enityty(ただの番号)
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include <cstdint>
#include <limits>

namespace TE
{
	/// @brief Entity自体はデータを持たない「番号」
	/// @brief データはコンポーネントとして Worldが持つ
	struct Entity
	{
		static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

		std::uint32_t index = kInvalidIndex;
		std::uint32_t generation = 0;

		constexpr bool IsNull() const { return index == kInvalidIndex; }

		friend constexpr bool operator==(const Entity& a, const Entity& b)
		{
			return a.index == b.index && a.generation == b.generation;
		}
		friend constexpr bool operator!=(const Entity& a, const Entity& b)
		{
			return !(a == b);
		}
	};

	inline constexpr Entity kNullEntity{};
}