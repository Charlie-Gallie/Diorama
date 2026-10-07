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
	};

	static_assert(sizeof(SceneData) == 912, "SceneData must match the shader cbuffer layout");
}
