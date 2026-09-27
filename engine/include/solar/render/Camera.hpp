#pragma once

#include "solar/core/Math.hpp"

namespace solar
{
	struct Camera
	{
		Vec3 position = { 0.0f, 0.0f, 0.0f };

		float yaw = 0.0f;
		float pitch = 0.0f;

		float fovY = glm::radians(30.0f);
	};

	// Default has camera looking down -Z
	struct CameraBasis
	{
		Vec3 right = { 1.0f, 0.0f, 0.0f };
		Vec3 up = { 0.0f, 1.0f, 0.0f };
		Vec3 forward = { 0.0f, 0.0f, -1.0f };
	};

	inline CameraBasis GetCameraBasis(const Camera& in_camera)
	{
		const float sinYaw = glm::sin(in_camera.yaw);
		const float cosYaw = glm::cos(in_camera.yaw);
		const float sinPitch = glm::sin(in_camera.pitch);
		const float cosPitch = glm::cos(in_camera.pitch);

		CameraBasis basis;

		basis.forward =
		{
			sinYaw * cosPitch,
			sinPitch,
			-cosYaw * cosPitch
		};

		const Vec3 worldUp = { 0.0f, 1.0f, 0.0f };
		basis.right = normalize(cross(basis.forward, worldUp));

		basis.up = cross(basis.right, basis.forward);

		return basis;
	}
}
