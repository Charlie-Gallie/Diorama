#pragma once

namespace solar
{
	// Scene shading controls, reaching the fragment shader as uniforms so they can
	// be changed without a rebuild. Temporary in the sense that the ones worth
	// keeping should become ConsoleVars.
	struct ShadingSettings
	{
		// A shading normal taken from the surrounding voxels rather than the face
		// the ray entered. Off is flat per-face shading.
		bool smoothNormals = true;

		// The radius the smooth shading normal is measured over, in voxels. Too
		// small and it picks up the voxel staircase as speckle; too large and
		// separate surfaces start bending towards each other.
		float normalRadius = 1.5f;

		// Voxels per pixel at which smoothing is half strength. Below it a voxel is
		// large enough on screen to be legible geometry, and smoothing reads as a
		// blurred upscale; above it the facets are sub-pixel noise worth removing.
		float normalFadeVoxels = 0.6f;

		// The two per-pixel costs that can be removed without changing the geometry, so turning
		// each off against the frame time is the only profiling available while SDL_GPU has no
		// timestamp queries
		bool shadows = true;
		bool ambientOcclusion = true;

		// Strength of the per-axis tint taken from the geometric normal, 0 to 1.
		// Three distinct values for the three axis directions is the cue that reads
		// as "voxels", and the smooth shading normal is what removes it.
		float axisTint = 0.25f;

		// How strongly a concave corner suppresses smoothing, 0 to 2. A floor
		// meeting a wall would otherwise be given a lit fillet neither surface has.
		float concavityGuard = 1.0f;
	};
}
