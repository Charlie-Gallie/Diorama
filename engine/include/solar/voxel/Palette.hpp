#pragma once

#include "solar/core/Math.hpp"

#include <array>

namespace solar::voxel
{
	inline constexpr size_t paletteSize = 256;

	struct Palette
	{
		// Linear, not sRGB
		// Converted on load so lighting maths never has to know where the colours came from
		// Entry 0 is air and unused
		std::array<Vec4, paletteSize> colours{};
	};
}
