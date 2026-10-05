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

		// Metres per second. World units are metres now, so these need no conversion
		constexpr float walkSpeed = 4.0f;
		constexpr float fastSpeed = 16.0f;

		// Roughly chest height on the subject, which is what the presets look at
		constexpr Vec3 sceneTarget = { 0.0f, 0.9f, 0.0f };

		// Stood clear of whatever the subject is rather than at a fixed spot, because the subjects
		// are different sizes and the point of the figure is to be beside one, not inside it
		constexpr float figureClearance = 0.9f;
		constexpr float figureDepth = 0.4f;
		constexpr float figureVoxelSize = 1.0f / 40.0f;
		constexpr const char* figurePath = "assets/models/test/figure_40.vox";

		// Debug blocks of a known size, kept in the scene because a shape that is corners and
		// holes at several scales at once is where shading problems show up first.
		//
		// Uploaded once as a 1 m cube and instanced twice, the second at half scale. One voxel
		// per Menger cell means iteration 3 is 27 across whatever it fills, so the two end up at
		// 27 and 54 voxels per metre from the same grid
		constexpr const char* mengerPath = "assets/models/test/menger_3.vox";
		constexpr float mengerVoxelSize = 1.0f / 27.0f;
		constexpr float mengerSmallScale = 0.5f;

		// Clear of the subject on the opposite side to the figure, with the small one set forward
		// of the large one rather than further out, so both stay in a default framing
		constexpr float mengerClearance = 1.0f;
		constexpr Vec3 mengerSmallOffset = { -0.15f, 0.0f, 1.05f };

		// Everything here is hard coded because there is no scene description yet, which is the
		// point at which these stop being lists. There are fewer models than instances because
		// the two sponges share one
		constexpr int sceneModelCount = 3;
		constexpr int sceneInstanceCount = 4;

		constexpr int cameraPresetCount = 6;
		constexpr int modelCount = 2;

		// Each model carries its own densities because a .vox axis cannot go over 256. The shapes
		// scene is 6.2 m along its longest axis so it cannot do 50 voxels per metre, but the car
		// at 4.3 m has room for it
		constexpr int maxDensityCount = 5;

		constexpr const char* modelNames[modelCount] = { "Shapes", "Car" };
		constexpr int densityCounts[modelCount] = { 4, 5 };

		constexpr const char* densityNames[modelCount][maxDensityCount] = {
			{ "40", "30", "20", "10", nullptr },
			{ "50", "40", "30", "20", "10" }
		};

		// The same numbers again as values, because voxel size is what the renderer needs and a
		// label is what the panel needs
		constexpr int densityValues[modelCount][maxDensityCount] = {
			{ 40, 30, 20, 10, 0 },
			{ 50, 40, 30, 20, 10 }
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

		// Presets are described by the vertical extent they frame, because that is what is actually
		// being chosen. The distance then falls out of it and the field of view. A narrow field of
		// view from a long way back is how the near-orthographic look is reached without an
		// orthographic projection
		Camera MakeViewPreset(
			const Vec3& in_target,
			float in_yawDegrees,
			float in_pitchDegrees,
			float in_viewMetres,
			float in_fovYDegrees
		) {
			const float distance = OrbitDistanceForExtent(in_viewMetres, glm::radians(in_fovYDegrees));

			return MakeOrbitCamera(in_target, in_yawDegrees, in_pitchDegrees, distance, in_fovYDegrees);
		}

		// Built on first use rather than at namespace scope, because the angles go through sin and
		// cos so they cannot be a constant expression
		const CameraPreset* CameraPresets()
		{
			static const CameraPreset presets[cameraPresetCount] = {
				{ "Gameplay",      "9 m of height in view, 30 deg",   MakeViewPreset(sceneTarget, 45.0f, -35.0f,  9.0f, 30.0f) },
				{ "Gameplay wide", "18 m of height in view, 30 deg",  MakeViewPreset(sceneTarget, 45.0f, -35.0f, 18.0f, 30.0f) },
				{ "Near ortho",    "9 m framed from far back, 6 deg", MakeViewPreset(sceneTarget, 45.0f, -35.0f,  9.0f,  6.0f) },
				{ "Steep",         "7 m, pitched down for indoors",   MakeViewPreset(sceneTarget, 45.0f, -58.0f,  7.0f, 30.0f) },
				{ "Eye level",     "3 m, stood on the ground",        MakeViewPreset(sceneTarget, 24.0f,  -4.0f,  3.0f, 32.0f) },
				{ "Front",         "5 m, flat on for silhouettes",    MakeViewPreset(sceneTarget,  0.0f,  -8.0f,  5.0f, 25.0f) }
			};

			return presets;
		}
	}
	Error DebugClient::Initialize(Renderer& inout_renderer)
	{
		camera = CameraPresets()[0].camera;

		debugUi.camera = &camera;
		debugUi.shading = &shading;
		debugUi.lights = &lights;
		debugUi.atmosphere = &atmosphere;
		debugUi.modelIndex = &modelIndex;
		debugUi.densityIndex = &densityIndex;
		debugUi.cameraPresets = CameraPresets();
		debugUi.cameraPresetCount = cameraPresetCount;
		debugUi.modelNames = modelNames;
		debugUi.modelCount = modelCount;

		// Loaded here rather than on the first Update, so a missing asset fails at startup with a
		// clear message instead of giving us a black first frame
		const Error loadError = LoadScene(inout_renderer);

		if (loadError != Error::NONE)
		{
			return loadError;
		}

		appliedModelIndex = modelIndex;
		appliedDensityIndex = densityIndex;

		return Error::NONE;
	}

	Error DebugClient::LoadScene(Renderer& inout_renderer)
	{
		// A .vox file says nothing about how big a voxel is, so every density here comes from the
		// code rather than from the data
		const struct
		{
			const char* path;
			float voxelSize;
		} sources[sceneModelCount] = {
			{ modelPaths[modelIndex][densityIndex], 1.0f / densityValues[modelIndex][densityIndex] },
			{ figurePath, figureVoxelSize },
			{ mengerPath, mengerVoxelSize }
		};

		// The grids have to outlive the upload, so they stay in scope until it has happened
		voxel::VoxelGrid grids[sceneModelCount];
		voxel::Palette palettes[sceneModelCount];
		VoxelModel models[sceneModelCount];

		for (int index = 0; index < sceneModelCount; ++index)
		{
			if (voxel::ImportVoxFile(sources[index].path, grids[index], palettes[index]) != Error::NONE)
			{
				SOLAR_LOG_ERR(CORE, "Failed to load {}", sources[index].path);
				return Error::INVALID_DATA;
			}

			models[index].grid = &grids[index];
			models[index].palette = &palettes[index];
			models[index].voxelSize = sources[index].voxelSize;
		}

		const Error uploadError = inout_renderer.SetVoxelModels(models, sceneModelCount);

		if (uploadError != Error::NONE)
		{
			return uploadError;
		}

		// Placed either side of whatever the subject is rather than at fixed spots, because the
		// subjects are different sizes and the point of all of these is to be beside one
		const float subjectHalfWidth = grids[0].size.x * models[0].voxelSize * 0.5f;
		const float mengerX = subjectHalfWidth + mengerClearance;

		instances[0] = { .position = { 0.0f, 0.0f, 0.0f }, .model = 0 };

		instances[1] = {
			.position = { -(subjectHalfWidth + figureClearance), 0.0f, figureDepth },
			.yaw = glm::radians(-28.0f),
			.model = 1
		};

		instances[2] = { .position = { mengerX, 0.0f, 0.2f }, .model = 2 };

		// Turned and halved, because a cube is the one shape where both are obvious at a glance
		instances[3] = {
			.position = Vec3(mengerX, 0.0f, 0.0f) + mengerSmallOffset,
			.yaw = glm::radians(25.0f),
			.scale = mengerSmallScale,
			.model = 2
		};

		instanceCount = sceneInstanceCount;

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

		// Presets do not need latching because holding the key just keeps reapplying the same
		// camera. The rest are panel only
		if (input::IsKeyDown(input::Key::NUM_5)) { camera = CameraPresets()[0].camera; }
		if (input::IsKeyDown(input::Key::NUM_6)) { camera = CameraPresets()[1].camera; }

		// The same model resampled at several densities, so switching between them changes voxels
		// per metre and nothing else
		if (input::IsKeyDown(input::Key::NUM_7)) { densityIndex = 0; }
		if (input::IsKeyDown(input::Key::NUM_8)) { densityIndex = 1; }
		if (input::IsKeyDown(input::Key::NUM_9)) { densityIndex = 2; }
		if (input::IsKeyDown(input::Key::NUM_0)) { densityIndex = 3; }

		UpdateFlyCamera(inout_window, in_deltaTime);
		UpdateSelection(inout_renderer);

		inout_renderer.SetShadingSettings(shading);
		inout_renderer.SetLightSettings(lights);
		inout_renderer.SetAtmosphereSettings(atmosphere);
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

		const float speed = input::IsKeyDown(input::Key::LEFT_SHIFT)
			? fastSpeed
			: walkSpeed;

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
		// Models do not all offer the same densities, so the panel is told which list applies
		// before it draws and the index is kept inside it
		debugUi.densityNames = densityNames[modelIndex];
		debugUi.densityCount = densityCounts[modelIndex];

		densityIndex = glm::clamp(densityIndex, 0, densityCounts[modelIndex] - 1);
		debugUi.density = static_cast<float>(densityValues[modelIndex][densityIndex]);

		if (modelIndex == appliedModelIndex && densityIndex == appliedDensityIndex)
		{
			return;
		}

		if (LoadScene(inout_renderer) == Error::NONE)
		{
			appliedModelIndex = modelIndex;
			appliedDensityIndex = densityIndex;

			SOLAR_LOG_INFO(RENDER, "{} at {} voxels per metre",
				modelNames[modelIndex], densityNames[modelIndex][densityIndex]);
		}
		else
		{
			modelIndex = appliedModelIndex;
			densityIndex = appliedDensityIndex;
		}
	}
}
