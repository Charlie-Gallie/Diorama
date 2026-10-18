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
		void UpdateHotReload(Renderer& inout_renderer, float in_deltaTime);

		// Uploads the selected model and the reference figure, then places both. Called again
		// whenever the panel picks a different model or density
		[[nodiscard]] Error LoadScene(Renderer& inout_renderer);

		Camera camera;
		ShadingSettings shading;
		LightSettings lights;
		AtmosphereSettings atmosphere;
		DebugUiContext debugUi;

		// A field of ground tiles with the detailed patch in the middle, then whichever test model
		// the panel has selected, a figure for human scale and two Menger sponges to shade against
		VoxelInstance instances[maxSceneInstances];
		int instanceCount = 0;

		// Every file the current scene was built from, so saving one in MagicaVoxel reloads it
		struct WatchedModel
		{
			const char* path = nullptr;
			int64_t loadedTime = 0;
			int64_t lastSeenTime = 0;
		};

		WatchedModel watched[maxSceneModels];
		int watchedCount = 0;
		float sinceLastPoll = 0.0f;

		int modelIndex = 1;
		int densityIndex = 0;

		// Start unset so the first load always happens, instead of assuming the renderer was
		// given a model behind our back
		int appliedModelIndex = -1;
		int appliedDensityIndex = -1;

		bool isLooking = false;
		bool smoothNormalsKeyWasDown = false;
		bool shadowsKeyWasDown = false;
		bool occlusionKeyWasDown = false;
	};
}
