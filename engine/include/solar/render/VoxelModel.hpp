#pragma once

#include "solar/core/Math.hpp"

namespace solar::voxel
{
	struct Palette;
	struct VoxelGrid;
}

namespace solar
{
	// A model handed to the renderer for upload. The grid says how many voxels there are and this
	// says how big one is, which is the only place world scale enters - the renderer never guesses
	// a size, and two models in the same scene may be authored at different densities
	struct VoxelModel
	{
		const voxel::VoxelGrid* grid = nullptr;
		const voxel::Palette* palette = nullptr;

		float voxelSize = 0.025f;
	};

	// Props stand on the ground, so the position is where the centre of the model's base goes
	// rather than its middle. Rotation is yaw only: a prop on flat ground needs no more, and
	// pitch or roll would want a quaternion and an inverse to go with it
	struct VoxelInstance
	{
		Vec3 position = { 0.0f, 0.0f, 0.0f };
		float yaw = 0.0f;

		// A multiplier on the model's voxel size, so one uploaded model can stand in the scene at
		// several sizes. Uniform only: anything else would stop the voxels being cubes, which the
		// traversal assumes, and would break the comparison of distances between instances
		float scale = 1.0f;

		int model = 0;
	};
}
