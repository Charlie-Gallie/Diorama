#pragma once

#include "solar/core/Math.hpp"
#include "solar/render/Camera.hpp"

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
	};

	static_assert(sizeof(CameraData) == 64, "CameraData must match the shader cbuffer layout");

	inline CameraData MakeCameraData(const Camera& in_camera, float in_aspect)
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

		return data;
	}
}
