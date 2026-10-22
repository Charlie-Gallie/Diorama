#pragma once

#include "solar/core/Assert.hpp"
#include "solar/core/Math.hpp"

#include <cstdint>
#include <vector>

namespace solar::voxel
{
	struct VoxelGrid
	{
		IVec3 size = { 0, 0, 0 };

		// One material index per voxel, 0 meaning air
		// Dense rather than sparse so it can be uploaded to an R8_UINT texture without conversion
		std::vector<uint8_t> materials;
	};

	// X varies fastest, then Y, then Z, matching the layout the GPU expects for a 3D texture upload
	inline size_t VoxelIndex(const IVec3& in_size, int in_x, int in_y, int in_z)
	{
		SOLAR_ASSERT(
			in_x >= 0 && in_y >= 0 && in_z >= 0 && in_x < in_size.x && in_y < in_size.y && in_z < in_size.z,
			"Voxel ({}, {}, {}) is outside a {}x{}x{} grid", in_x, in_y, in_z, in_size.x, in_size.y, in_size.z
		);

		return (static_cast<size_t>(in_z) * in_size.y + in_y) * in_size.x + in_x;
	}
}
