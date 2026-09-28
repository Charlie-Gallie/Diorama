#pragma once

#include "solar/core/Error.hpp"
#include "solar/voxel/Palette.hpp"
#include "solar/voxel/VoxelGrid.hpp"

#include <cstdint>

namespace solar::voxel
{
	// Parses a MagicaVoxel .vox file. Only the first model in the file is read
	// Coordinates are converted from MagicaVoxel's Z-up to the engine's Y-up
	[[nodiscard]] Error ImportVox(
		const uint8_t* in_data,
		size_t in_size,
		VoxelGrid& out_grid,
		Palette& out_palette
	);

	// Convenience wrapper reading the file through solar::filesystem.
	[[nodiscard]] Error ImportVoxFile(const char* in_path, VoxelGrid& out_grid, Palette& out_palette);
}
