#pragma once

#include "solar/core/Math.hpp"
#include "solar/render/VoxelModel.hpp"

namespace solar
{

	// Mirrors the SceneData cbuffer in engine/shaders/Fullscreen.frag.hlsl byte for byte
	struct SceneData
	{
		Vec4 counts = { 0.0f, 0.0f, 0.0f, 0.0f }; // x instance count

		Vec4 modelOffset[maxSceneModels] = {}; // xyz atlas offset in voxels, w voxel size in metres
		Vec4 modelSize[maxSceneModels] = {}; // xyz size in voxels
		Vec4 modelCoarse[maxSceneModels] = {}; // xyz coarse atlas offset in cells, w voxels per cell

		Vec4 instanceTransform[maxSceneInstances] = {}; // xyz position in metres, w model index
		Vec4 instanceRotation[maxSceneInstances] = {}; // x sine of yaw, y cosine of yaw, z scale

		// A world-space sphere around each instance's box, so a ray that misses an instance can be
		// turned away before paying for the rotation into its local space and the box test's divides
		Vec4 instanceSphere[maxSceneInstances] = {}; // xyz centre in metres, w radius
	};

	static_assert(sizeof(SceneData) == 1936, "SceneData must match the shader cbuffer layout");
}
