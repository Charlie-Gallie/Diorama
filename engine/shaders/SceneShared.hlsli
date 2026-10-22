#pragma once

// Included by both the scene shader and the renderer, so the layouts and constants here are written
// once and cannot drift apart. Every member is a float4 because that is the one type both sides lay
// out identically, in a cbuffer and a storage buffer alike, with no packing rules to remember

#ifdef __cplusplus
	#include "solar/core/Math.hpp"

namespace solar::shader
{
	using float4 = Vec4;
#endif

	// Voxels per side of a coarse cell. 16 is the measured optimum: 4, 8, 16 and 32 gave roughly
	// 14.5, 10.3, 7.4 and 10.8 ms on the same scene. Too small and the coarse walk costs as much as
	// it saves, too large and a cell holding one voxel still drags the ray through all of it
	static const int coarseCell = 16;

	// Steps the fine walk may take inside one coarse cell. It has to cover the cell's diagonal,
	// which is three cells' worth of axis crossings, so it follows the cell size
	static const int maxFineSteps = 4 * coarseCell;

	// One entry per uploaded model, in a storage buffer rebuilt alongside the atlas
	struct ModelData
	{
		float4 offset; // xyz atlas offset in voxels, w voxel size in metres
		float4 size; // xyz size in voxels
		float4 coarse; // xyz coarse atlas offset in cells, w voxels per cell
	};

	// One entry per instance, in a storage buffer refilled every frame nearest first
	struct InstanceData
	{
		float4 transform; // xyz position in metres, w model index
		float4 rotation; // x sine of yaw, y cosine of yaw, z scale

		// A world-space sphere around the instance's box, so a ray that misses it can be turned
		// away before paying for the rotation into local space and the box test's divides
		float4 sphere; // xyz centre in metres, w radius
	};

	// The storage buffers are sized for capacity rather than for what is in them, so how much is
	// in use travels here. This is also the cheap place for any other per-frame scene value
	struct SceneConstants
	{
		float4 counts; // x instance count
	};

#ifdef __cplusplus
}
#endif
