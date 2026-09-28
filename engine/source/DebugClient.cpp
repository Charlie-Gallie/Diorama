#include "DebugClient.hpp"

#include "solar/core/Log.hpp"
#include "solar/platform/Input.hpp"
#include "solar/platform/Window.hpp"
#include "solar/render/Renderer.hpp"
#include "solar/voxel/VoxImport.hpp"

namespace solar
{
	namespace
	{
		// Radians per pixel of mouse motion
		constexpr float lookSensitivity = 0.0025f;

		constexpr float minPitch = glm::radians(-89.9f);
		constexpr float maxPitch = glm::radians(89.9f);

		// The scene shader normalises a model so its longest axis spans -1..1, so world units
		// depend on what is loaded. The test models are 6.2 m and 4.3 m along that axis, so one
		// unit is a little over 3 m. We only use this to talk about the camera in metres, so an
		// approximation is fine.
		constexpr float metresPerUnit = 3.1f;

		constexpr float walkSpeedMetres = 4.0f;
		constexpr float fastSpeedMetres = 16.0f;

		// Roughly the middle of the shapes scene, a little above the floor
		constexpr Vec3 sceneTarget = { 0.0f, -0.11f, 0.0f };

		// The blue sphere on its front row, for looking at one object closely
		constexpr Vec3 sphereTarget = { -0.74f, -0.11f, 0.24f };

		constexpr int cameraPresetCount = 6;
		constexpr int modelCount = 2;

		// Each model carries its own densities because a .vox axis can't go over 256. The shapes
		// scene is 6.2 m along its longest axis so it can't do 50 voxels per metre, but the car
		// at 4.3 m has room for it.
		constexpr int maxDensityCount = 5;

		constexpr const char* modelNames[modelCount] = { "Shapes", "Car" };
		constexpr int densityCounts[modelCount] = { 4, 5 };

		constexpr const char* densityNames[modelCount][maxDensityCount] = {
			{ "40", "30", "20", "10", nullptr },
			{ "50", "40", "30", "20", "10" }
		};

		constexpr const char* modelPaths[modelCount][maxDensityCount] = {
			{
				"assets/models/test/density_40.vox",
				"assets/models/test/density_30.vox",
				"assets/models/test/density_20.vox",
				"assets/models/test/density_10.vox",
				nullptr
			},
			{
				"assets/models/test/car_50.vox",
				"assets/models/test/car_40.vox",
				"assets/models/test/car_30.vox",
				"assets/models/test/car_20.vox",
				"assets/models/test/car_10.vox"
			}
		};

		// Presets are described by the vertical extent they frame in metres, because that's what
		// we're actually choosing. The distance then falls out of it and the field of view.
		// A narrow field of view from a long way back is how we get the near-orthographic look
		// without an orthographic projection.
		Camera MakeViewPreset(
			const Vec3& in_target,
			float in_yawDegrees,
			float in_pitchDegrees,
			float in_viewMetres,
			float in_fovYDegrees
		) {
			const float distance = OrbitDistanceForExtent(
				in_viewMetres / metresPerUnit,
				glm::radians(in_fovYDegrees)
			);

			return MakeOrbitCamera(in_target, in_yawDegrees, in_pitchDegrees, distance, in_fovYDegrees);
		}

		// Built on first use rather than at namespace scope, because the angles go through sin
		// and cos so they can't be a constant expression. The targets are in world units and
		// assume the test models' proportions.
		const CameraPreset* CameraPresets()
		{
			static const CameraPreset presets[cameraPresetCount] = {
				{ "Gameplay",      "9 m of height in view, 30 deg",   MakeViewPreset(sceneTarget,  45.0f, -35.0f,  9.0f, 30.0f) },
				{ "Gameplay wide", "18 m of height in view, 30 deg",  MakeViewPreset(sceneTarget,  45.0f, -35.0f, 18.0f, 30.0f) },
				{ "Near ortho",    "9 m framed from far back, 6 deg", MakeViewPreset(sceneTarget,  45.0f, -35.0f,  9.0f,  6.0f) },
				{ "Steep",         "7 m, pitched down for indoors",   MakeViewPreset(sceneTarget,  45.0f, -58.0f,  7.0f, 30.0f) },
				{ "Prop close-up", "2 m, on the blue sphere",         MakeViewPreset(sphereTarget, 25.0f, -18.0f,  2.0f, 35.0f) },
				{ "Front",         "5 m, flat on for silhouettes",    MakeViewPreset(sceneTarget,   0.0f,  -8.0f,  5.0f, 25.0f) }
			};

			return presets;
		}

		[[nodiscard]] Error LoadModel(Renderer& inout_renderer, const char* in_path)
		{
			voxel::VoxelGrid grid;
			voxel::Palette palette;

			const Error importError = voxel::ImportVoxFile(in_path, grid, palette);

			if (importError != Error::NONE)
			{
				return importError;
			}

			return inout_renderer.SetVoxelModel(grid, palette);
		}
	}

	Error DebugClient::Initialize(Renderer& inout_renderer)
	{
		camera = CameraPresets()[0].camera;

		debugUi.camera = &camera;
		debugUi.shading = &shading;
		debugUi.modelIndex = &modelIndex;
		debugUi.densityIndex = &densityIndex;
		debugUi.cameraPresets = CameraPresets();
		debugUi.cameraPresetCount = cameraPresetCount;
		debugUi.modelNames = modelNames;
		debugUi.modelCount = modelCount;
		debugUi.metresPerUnit = metresPerUnit;

		// Load here rather than on the first Update, so a missing asset fails at startup with a
		// clear message instead of giving us a black first frame
		const Error loadError = LoadModel(inout_renderer, modelPaths[modelIndex][densityIndex]);

		if (loadError != Error::NONE)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to load {}", modelPaths[modelIndex][densityIndex]);
			return loadError;
		}

		appliedModelIndex = modelIndex;
		appliedDensityIndex = densityIndex;

		return Error::NONE;
	}

	void DebugClient::Update(Window& inout_window, Renderer& inout_renderer, float in_deltaTime)
	{
		// Latched so holding the key toggles once rather than every frame
		const bool smoothNormalsKeyDown = input::IsKeyDown(input::Key::NUM_1);

		if (smoothNormalsKeyDown && smoothNormalsKeyWasDown == false)
		{
			shading.smoothNormals = !shading.smoothNormals;
			SOLAR_LOG_INFO(RENDER, "Smooth normals {}", shading.smoothNormals ? "on" : "off");
		}

		smoothNormalsKeyWasDown = smoothNormalsKeyDown;

		// Presets don't need latching because holding the key just keeps reapplying the same
		// camera. The rest are panel only.
		if (input::IsKeyDown(input::Key::NUM_5)) { camera = CameraPresets()[0].camera; }
		if (input::IsKeyDown(input::Key::NUM_6)) { camera = CameraPresets()[1].camera; }

		// The same model resampled at several densities, so switching between them changes
		// voxels per metre and nothing else
		if (input::IsKeyDown(input::Key::NUM_7)) { densityIndex = 0; }
		if (input::IsKeyDown(input::Key::NUM_8)) { densityIndex = 1; }
		if (input::IsKeyDown(input::Key::NUM_9)) { densityIndex = 2; }
		if (input::IsKeyDown(input::Key::NUM_0)) { densityIndex = 3; }

		UpdateFlyCamera(inout_window, in_deltaTime);
		UpdateSelection(inout_renderer);

		inout_renderer.SetShadingSettings(shading);
	}

	void DebugClient::UpdateFlyCamera(Window& inout_window, float in_deltaTime)
	{
		const bool wantsLook = input::IsMouseButtonDown(input::MouseButton::RIGHT);

		if (wantsLook != isLooking)
		{
			isLooking = wantsLook;
			inout_window.SetRelativeMouseMode(isLooking);
		}

		// Consumed every frame whether we're looking or not, so motion from while the button was
		// up doesn't arrive as a jump when it goes down
		const Vec2 mouseDelta = input::ConsumeMouseDelta();

		if (isLooking)
		{
			camera.yaw += mouseDelta.x * lookSensitivity;
			camera.pitch -= mouseDelta.y * lookSensitivity;

			// At exactly +-90 degrees forward is parallel to world up and the cross product that
			// gives us right collapses to zero
			camera.pitch = glm::clamp(camera.pitch, minPitch, maxPitch);
		}

		const CameraBasis basis = GetCameraBasis(camera);

		const float speedMetres = input::IsKeyDown(input::Key::LEFT_SHIFT)
			? fastSpeedMetres
			: walkSpeedMetres;

		const float speed = speedMetres / metresPerUnit;

		Vec3 move = { 0.0f, 0.0f, 0.0f };

		if (input::IsKeyDown(input::Key::W)) { move += basis.forward; }
		if (input::IsKeyDown(input::Key::S)) { move -= basis.forward; }
		if (input::IsKeyDown(input::Key::D)) { move += basis.right; }
		if (input::IsKeyDown(input::Key::A)) { move -= basis.right; }
		if (input::IsKeyDown(input::Key::E)) { move += basis.up; }
		if (input::IsKeyDown(input::Key::Q)) { move -= basis.up; }

		// Normalising keeps diagonals the same speed as straight lines, and the zero check stops
		// us normalising a zero vector
		if (move != Vec3(0.0f))
		{
			camera.position += normalize(move) * speed * in_deltaTime;
		}
	}

	void DebugClient::UpdateSelection(Renderer& inout_renderer)
	{
		// Models don't all offer the same densities, so we tell the panel which list applies
		// before it draws and keep the index inside it
		debugUi.densityNames = densityNames[modelIndex];
		debugUi.densityCount = densityCounts[modelIndex];

		densityIndex = glm::clamp(densityIndex, 0, densityCounts[modelIndex] - 1);

		if (modelIndex == appliedModelIndex && densityIndex == appliedDensityIndex)
		{
			return;
		}

		const char* const path = modelPaths[modelIndex][densityIndex];

		if (LoadModel(inout_renderer, path) == Error::NONE)
		{
			appliedModelIndex = modelIndex;
			appliedDensityIndex = densityIndex;

			SOLAR_LOG_INFO(RENDER, "{} at {} voxels per metre",
				modelNames[modelIndex], densityNames[modelIndex][densityIndex]);
		}
		else
		{
			SOLAR_LOG_ERR(RENDER, "Failed to load {}", path);

			modelIndex = appliedModelIndex;
			densityIndex = appliedDensityIndex;
		}
	}
}
