#pragma once

#include "solar/core/DebugUiHook.hpp"
#include "solar/core/Error.hpp"
#include "solar/render/AtmosphereSettings.hpp"
#include "solar/render/Camera.hpp"
#include "solar/render/LightSettings.hpp"
#include "solar/render/ShadingSettings.hpp"
#include "solar/render/VoxelModel.hpp"

namespace solar
{
	class Renderer;
	class Window;

	// A stand-in for the client, which doesn't exist yet. It owns the fly camera, the shading
	// switches and the model browser we use to make look and density decisions, and fills in the
	// DebugUiContext that the devtools panel draws.
	//
	// It only lives in the engine because nothing else can hold it yet. Keeping it in one place
	// instead of scattered through Engine::Run means it can move to the client in one go.
	class DebugClient
	{
	public:
		[[nodiscard]] Error Initialize(Renderer& inout_renderer);

		// Reads input and applies anything the debug panel changed last frame
		void Update(Window& inout_window, Renderer& inout_renderer, float in_deltaTime);

		const Camera& GetCamera() const { return camera; }

		const VoxelInstance* GetInstances() const { return instances; }
		int GetInstanceCount() const { return instanceCount; }
		DebugUiContext& GetDebugUi() { return debugUi; }

	private:
		void UpdateFlyCamera(Window& inout_window, float in_deltaTime);
		void UpdateSelection(Renderer& inout_renderer);

		// Uploads the selected model and the reference figure, then places both. Called again
		// whenever the panel picks a different model or density
		[[nodiscard]] Error LoadScene(Renderer& inout_renderer);

		Camera camera;
		ShadingSettings shading;
		LightSettings lights;
		AtmosphereSettings atmosphere;
		DebugUiContext debugUi;

		// The subject is whichever test model the panel has selected, plus a figure for human
		// scale and two Menger sponges of a known size to shade against
		VoxelInstance instances[4];
		int instanceCount = 0;

		int modelIndex = 0;
		int densityIndex = 0;

		// Start unset so the first load always happens, instead of assuming the renderer was
		// given a model behind our back
		int appliedModelIndex = -1;
		int appliedDensityIndex = -1;

		bool isLooking = false;
		bool smoothNormalsKeyWasDown = false;
	};
}
