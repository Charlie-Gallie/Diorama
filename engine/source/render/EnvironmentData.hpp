#pragma once

#include "AtmosphereData.hpp"
#include "LightData.hpp"

namespace solar
{
	// SDL_GPU allows four uniform buffers per shader stage, and the scene shader already spends
	// three on the camera, the palette and the instances. Lights and atmosphere stay separate
	// structs on this side because they are separate subjects, and share one cbuffer on the other
	// because the hardware says so
	struct EnvironmentData
	{
		LightData light;
		AtmosphereData atmosphere;
	};

	static_assert(sizeof(EnvironmentData) == 144, "EnvironmentData must match the shader cbuffer layout");

	inline EnvironmentData MakeEnvironmentData(
		const LightSettings& in_lights,
		const AtmosphereSettings& in_atmosphere
	) {
		EnvironmentData data;
		data.light = MakeLightData(in_lights);
		data.atmosphere = MakeAtmosphereData(in_atmosphere);

		return data;
	}
}
