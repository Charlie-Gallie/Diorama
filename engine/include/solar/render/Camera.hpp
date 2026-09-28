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

	// How far back a camera has to sit for a given vertical extent to fill the view.
	// Field of view is an angle, so the extent a camera covers is set by where it is, which is the
	// relationship this inverts.
	inline float OrbitDistanceForExtent(float in_extent, float in_fovY)
	{
		return (in_extent * 0.5f) / glm::tan(in_fovY * 0.5f);
	}

	// A camera looking at a point from a given direction and distance.
	// Both the debug presets and the eventual gameplay camera are orbits around a target.
	inline Camera MakeOrbitCamera(
		const Vec3& in_target,
		float in_yawDegrees,
		float in_pitchDegrees,
		float in_distance,
		float in_fovYDegrees
	) {
		Camera camera;
		camera.yaw = glm::radians(in_yawDegrees);
		camera.pitch = glm::radians(in_pitchDegrees);
		camera.fovY = glm::radians(in_fovYDegrees);

		// The basis only depends on the angles, so forward is known before the
		// position is, and the position is just a step backwards along it
		camera.position = in_target - GetCameraBasis(camera).forward * in_distance;

		return camera;
	}

	struct CameraPreset
	{
		const char* name = nullptr;
		const char* hint = nullptr;
		Camera camera;
	};
}
