#pragma once

#include "solar/core/Math.hpp"

namespace solar
{
	// The light rig, reaching the scene shader as uniforms so it can be tuned without a rebuild.
	// Three terms that differ in colour as much as in direction. A warm key against a cool sky is
	// most of why lighting reads as authored rather than as a lamp in a grey box, and it is the
	// contrast rather than any one value that does the work.
	//
	// Colours are linear, because that is the space lighting is done in. They are not what a
	// colour picker would show for the same light.
	struct LightSettings
	{
		// Direction towards the key, as angles rather than a vector, because angles are what a
		// slider wants and the vector falls out of them. Azimuth follows the camera's yaw
		// convention so the same number means the same thing in both places.
		// The default is the direction the single hard-coded light used, so only colour changes
		float keyAzimuth = glm::radians(42.0f);
		float keyElevation = glm::radians(53.0f);

		// Warm, standing in for the sun. The only term the shadow ray cuts
		Vec3 keyColour = { 1.0f, 0.86f, 0.68f };
		float keyIntensity = 1.0f;

		// Cool, and the upper end of the hemisphere the ambient is taken from
		Vec3 skyColour = { 0.35f, 0.45f, 0.65f };
		float skyIntensity = 0.35f;

		// Bounce off the ground, so it is tinted by whatever is down there and arrives from below.
		// A vertical surface sits exactly halfway along the hemisphere, so this is half of all the
		// ambient anything upright receives - at the weak values it started with, the side of a
		// car read darker than the floor it stood on.
		//
		// Cool and near neutral rather than warm, matching the pale floor the test scenes stand
		// on. Still less blue than the sky so up and down stay distinguishable. A world of soil
		// and grass would want this warm again, which is the argument for eventually taking it
		// from whatever is actually underneath rather than from a constant
		Vec3 groundColour = { 0.26f, 0.28f, 0.32f };
		float groundIntensity = 0.35f;

		// What the ambient falls to where occlusion is total. Not zero, or a fully enclosed
		// crevice goes pure black and loses its material
		float occlusionFloor = 0.15f;

		// Scene exposure, applied in the present pass ahead of the tonemap. It belongs with post
		// settings rather than with the rig, but the two are tuned as a pair and one float does
		// not earn a struct of its own yet
		float exposure = 0.9f;
	};
}
