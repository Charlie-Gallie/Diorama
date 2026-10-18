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
		Vec3 keyColour = { 0.8f, 0.75f, 0.70f };
		float keyIntensity = 1.0f;

		// How far the key wraps past the terminator, 0 to 1. Zero is plain Lambert, where a
		// surface turning away from the key loses it very fast - over a one voxel step in grass
		// the shading normal swings 40 or 50 degrees in a voxel and a half, and plain Lambert
		// turns that into a hard dark edge. Wrapping lifts the far side so a step reads as a
		// change in the surface rather than as a cut in it.
		//
		// It stands in for light that has bounced or scattered through the surface, which is
		// most of what stops real grass and foliage having a hard terminator
		float keyWrap = 0.5f;

		// Cool, and the upper end of the hemisphere the ambient is taken from
		Vec3 skyColour = { 0.53f, 0.60f, 0.86f };
		float skyIntensity = 0.50f;

		// Bounce off the ground, so it is tinted by whatever is down there and arrives from below.
		// A vertical surface sits exactly halfway along the hemisphere, so this is half of all the
		// ambient anything upright receives - at the weak values it started with, the side of a
		// car read darker than the floor it stood on.
		//
		// Green rather than blue, so up and down stay distinguishable where the sky is doing most
		// of the ambient work. It no longer matches the grey test floor, which is the argument for
		// eventually taking it from whatever is actually underneath rather than from a constant
		Vec3 groundColour = { 0.60f, 0.68f, 0.65f };
		float groundIntensity = 0.20f;

		// What the ambient falls to where occlusion is total. Not zero, or a fully enclosed
		// crevice goes pure black and loses its material
		float occlusionFloor = 0.15f;

		// How strongly an occluder's own colour fills the crevice it makes, as a multiplier on
		// its albedo. One bounce undercounts what happens between two nearby surfaces, so this
		// is above 1 by default. Zero gives the flat neutral darkening it used to have, which
		// makes it the A/B for whether the colour is doing anything
		float occlusionBounce = 2.0f;

		// How far a shadow ray looks for an occluder. A direct trade of shadow distance against
		// frame time, since a shorter ray turns more instances away by distance before any of
		// them are traversed - and past the fog there is nothing left to cast a shadow onto
		// anyway. Zero switches shadows off entirely
		float shadowRange = 120.0f;

		// How far along the ray to start looking, in metres. Detail smaller than this cannot
		// shadow itself.
		//
		// A shadow ray is all or nothing, so a feature whose shadow is narrower than a pixel
		// does not make that pixel slightly darker - it takes the key away completely and leaves
		// a speck at about half the brightness of its neighbours. A one voxel step in grass
		// casts a shadow 1.9 cm long, which is half a pixel at the usual framing, and the field
		// comes out stippled with them. Starting the ray past that keeps the shadows big enough
		// to actually read and drops the ones that only alias.
		//
		// 3 cm was settled by eye and turns out to be the geometric answer: clearing a step of
		// height h needs h / sin(elevation), which for one 2.5 cm voxel at the default key is
		// 3.13 cm. So this is about the smallest skip that does the job, which is what we want -
		// it removes the stippling and nothing larger. Note it follows the key: drop the
		// elevation and the same step needs a longer skip
		//
		// The cost is contact shadow: nothing closer than this darkens anything. Ambient
		// occlusion is what covers that, and it is gathered rather than sampled once, so it has
		// no such cliff
		float shadowSkip = 0.030f;

		// Scene exposure, applied in the present pass ahead of the tonemap. It belongs with post
		// settings rather than with the rig, but the two are tuned as a pair and one float does
		// not earn a struct of its own yet
		float exposure = 0.9f;
	};
}
