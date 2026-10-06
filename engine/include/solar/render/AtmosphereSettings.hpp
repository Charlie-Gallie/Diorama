#pragma once

#include "solar/core/Math.hpp"

namespace solar
{
	// The sky the camera sees, the ground it stands on and the air in between.
	// Kept apart from LightSettings because these say what is drawn rather than what lights it - the ambient sky
	// term there is an average over the whole dome, which is not the colour of any part of it.
	// Colours are linear, as everywhere else
	struct AtmosphereSettings
	{
		Vec3 skyHorizon = { 0.52f, 0.58f, 0.70f };
		Vec3 skyZenith = { 0.15f, 0.26f, 0.48f };

		// Distance fades towards the sky in the ray's own direction rather than towards a single
		// fog colour. That is what makes ground meet the horizon instead of ending at it
		float fogDensity = 0.002f;

		bool groundEnabled = true;

		Vec3 groundLight = { 0.10f, 0.17f, 0.06f };
		Vec3 groundDark = { 0.08f, 0.13f, 0.05f };

		// Paving slab sized
		float groundTileMetres = 1.0f;
	};
}
