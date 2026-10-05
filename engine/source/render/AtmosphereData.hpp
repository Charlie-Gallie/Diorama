#pragma once

#include "solar/core/Math.hpp"
#include "solar/render/AtmosphereSettings.hpp"

namespace solar
{
	// Mirrors the AtmosphereData cbuffer in engine/shaders/Fullscreen.frag.hlsl byte for byte
	struct AtmosphereData
	{
		Vec4 skyHorizon = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz linear colour, w fog density
		Vec4 skyZenith = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz linear colour, w tile size in metres
		Vec4 groundLight = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz linear colour, w ground plane on
		Vec4 groundDark = { 0.0f, 0.0f, 0.0f, 0.0f }; // xyz linear colour
	};

	static_assert(sizeof(AtmosphereData) == 64, "AtmosphereData must match the shader cbuffer layout");

	inline AtmosphereData MakeAtmosphereData(const AtmosphereSettings& in_atmosphere)
	{
		AtmosphereData data;

		data.skyHorizon = Vec4(in_atmosphere.skyHorizon, in_atmosphere.fogDensity);
		data.skyZenith = Vec4(in_atmosphere.skyZenith, in_atmosphere.groundTileMetres);
		data.groundLight = Vec4(in_atmosphere.groundLight, in_atmosphere.groundEnabled ? 1.0f : 0.0f);
		data.groundDark = Vec4(in_atmosphere.groundDark, 0.0f);

		return data;
	}
}
