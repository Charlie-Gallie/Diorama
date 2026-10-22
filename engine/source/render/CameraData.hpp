#pragma once

#include "solar/core/Math.hpp"
#include "solar/render/Camera.hpp"
#include "solar/render/ShadingSettings.hpp"

namespace solar
{
	// Mirrors the cbuffer in engine/shaders/Fullscreen.frag.hlsl byte for byte
	// HLSL constant buffers are laid out in 16-byte rows and no member may go between a boundary
	struct CameraData
	{
		Vec4 positionAndScale = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz position, w tan(fovY/2)
		Vec4 rightAndAspect = { 1.0f, 0.0f, 0.0f, 1.0f }; // xyz right, w width/height
		Vec4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
		Vec4 forward = { 0.0f, 0.0f, -1.0f, 0.0f };
		Vec4 shadingA = { 0.0f, 0.0f, 0.0f, 0.0f }; // normal radius, normal fade, axis tint, smooth normals on
		Vec4 shadingB = { 0.0f, 0.0f, 0.0f, 0.0f }; // concavity guard, pixel angle, shadows on, occlusion on
	};

	static_assert(sizeof(CameraData) == 96, "CameraData must match the shader cbuffer layout");

	inline CameraData MakeCameraData(
		const Camera& in_camera,
		float in_aspect,
		int in_targetHeight,
		const ShadingSettings& in_shading
	)
	{
		const CameraBasis basis = GetCameraBasis(in_camera);

		// Field of view is an angle, but the ray needs a distance off-axis at one unit ahead
		// tan converts between the two.
		const float scale = glm::tan(in_camera.fovY * 0.5f);

		CameraData data;
		data.positionAndScale = Vec4(in_camera.position, scale);
		data.rightAndAspect = Vec4(basis.right, in_aspect);
		data.up = Vec4(basis.up, 0.0f);
		data.forward = Vec4(basis.forward, 0.0f);

		data.shadingA = Vec4(
			in_shading.normalRadius,
			in_shading.normalFadeVoxels,
			in_shading.axisTint,
			in_shading.smoothNormals ? 1.0f : 0.0f
		);

		// How much of the view one pixel subtends at a distance of one unit. Lets the
		// shader work out how large a voxel is on screen from the hit distance alone,
		// which screen-space derivatives would also give but not across a silhouette.
		const float pixelAngle = in_targetHeight > 0
			? 2.0f * scale / static_cast<float>(in_targetHeight)
			: 0.0f;

		data.shadingB = Vec4(
			in_shading.concavityGuard,
			pixelAngle,
			in_shading.shadows ? 1.0f : 0.0f,
			in_shading.ambientOcclusion ? 1.0f : 0.0f
		);

		return data;
	}
}
