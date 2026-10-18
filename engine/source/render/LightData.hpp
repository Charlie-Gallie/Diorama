#pragma once

#include "solar/core/Math.hpp"
#include "solar/render/LightSettings.hpp"

namespace solar
{
	// Mirrors the LightData cbuffer in engine/shaders/Fullscreen.frag.hlsl byte for byte.
	// HLSL constant buffers are laid out in 16-byte rows and no member may cross a boundary
	struct LightData
	{
		Vec4 keyDirection = { 0.0f, 1.0f, 0.0f, 0.0f }; // xyz unit direction towards the key, w intensity
		Vec4 keyColour = { 1.0f, 1.0f, 1.0f, 0.0f }; // xyz linear colour, w terminator wrap
		Vec4 skyColour = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz linear colour, w intensity
		Vec4 groundColour = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz linear colour, w intensity
		Vec4 ambient = { 0.0f, 0.0f, 0.0f, 0.0f }; // x occlusion floor, y shadow range, z occlusion bounce, w shadow skip
	};

	static_assert(sizeof(LightData) == 80, "LightData must match the shader cbuffer layout");

	inline LightData MakeLightData(const LightSettings& in_lights)
	{
		// The same basis convention as the camera, so an azimuth reads the same in both
		const float cosElevation = glm::cos(in_lights.keyElevation);

		const Vec3 direction = {
			glm::sin(in_lights.keyAzimuth) * cosElevation,
			glm::sin(in_lights.keyElevation),
			-glm::cos(in_lights.keyAzimuth) * cosElevation
		};

		LightData data;

		// Unit length by construction, so normalising only cleans up the error left by sin and
		// cos. Done here because the shader would otherwise pay for it once per pixel
		data.keyDirection = Vec4(normalize(direction), in_lights.keyIntensity);
		data.keyColour = Vec4(in_lights.keyColour, in_lights.keyWrap);
		data.skyColour = Vec4(in_lights.skyColour, in_lights.skyIntensity);
		data.groundColour = Vec4(in_lights.groundColour, in_lights.groundIntensity);
		data.ambient = Vec4(
			in_lights.occlusionFloor,
			in_lights.shadowRange,
			in_lights.occlusionBounce,
			in_lights.shadowSkip
		);

		return data;
	}
}
