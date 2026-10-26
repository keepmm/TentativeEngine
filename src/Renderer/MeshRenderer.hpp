/*****************************************************************//**
 * \file   MeshRenderer.hpp
 * \brief  メッシュを書くためのコンポーネント
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include "Math/Color.hpp"

#include <cstdint>
#include <limits>

namespace TE
{
	struct MeshHandle
	{
		std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
		constexpr bool IsValid() const { return index != std::numeric_limits<std::uint32_t>::max(); }
	};

	struct MeshRenderer
	{
		MeshHandle mesh;
		Color color = Color::white;
	};
}