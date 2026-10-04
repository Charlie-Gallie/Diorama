#pragma once

namespace solar
{
	struct Camera;
	struct CameraPreset;
	struct LightSettings;
	struct ShadingSettings;

	// What the debug UI is allowed to see and change.
	// The convention is that pointers are writable - devtools writes through them and the engine reads the
	// result on the next frame - and values are read-only readouts filled in by whoever knows them.
	// Forward declarations only, so this header stays free of any subsystem it describes.
	struct DebugUiContext
	{
		Camera* camera = nullptr;

		ShadingSettings* shading = nullptr;

		LightSettings* lights = nullptr;

		// Indices into modelNames and densityNames, applied by the engine on change
		int* modelIndex = nullptr;
		int* densityIndex = nullptr;

		const CameraPreset* cameraPresets = nullptr;
		int cameraPresetCount = 0;

		const char* const* modelNames = nullptr;
		int modelCount = 0;

		const char* const* densityNames = nullptr;
		int densityCount = 0;

		int sceneWidth = 0;
		int sceneHeight = 0;

		// World units are arbitrary - the scene shader normalises a model so its
		// longest axis spans -1..1 - so the panel needs this to report metres
		float metresPerUnit = 1.0f;
	};

	using DebugUiCallback = void(*)(DebugUiContext& inout_context);
}
