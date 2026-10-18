#include "DebugClient.hpp"

#include "solar/core/Filesystem.hpp"
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

		// How often the model files are checked for having been written. Often enough to feel
		// immediate, rarely enough that the cost of asking the OS never matters
		constexpr float hotReloadInterval = 0.25f;

		// A mock-up of the block world design: a 6 m patch of 0.5 m terrain blocks whose voxel
		// content is at the world density, with a hut, a tree, a path and a mound. Here to judge
		// the design, not to stay
		constexpr const char* patchPath = "assets/models/test/worldpatch.vox";
		constexpr float patchVoxelSize = 1.0f / 40.0f;

		// Its flat ground sits 1.5 m above the model's base, so this drops that onto y = 0
		constexpr float patchBase = -1.5f;

		// Plain ground either side of it. A .vox axis stops at 6.4 m, so filling a view means
		// instancing, and three variants turned a quarter at a time is what stops the repeat being
		// obvious - shared block content hashes on position inside its own model, so every copy of
		// one tile is identical
		constexpr const char* tilePaths[3] = {
			"assets/models/test/ground_0.vox",
			"assets/models/test/ground_1.vox",
			"assets/models/test/ground_2.vox"
		};

		constexpr float tileSize = 6.0f;
		constexpr float tileBase = -0.5f;
		constexpr int tileReach = 2;

		// Everything here is hard coded because there is no scene description yet, which is the
		// point at which these stop being lists. There are fewer models than instances because
		// the two sponges share one
		constexpr int sceneModelCount = 7;

		// A 5 by 5 of ground with the detailed patch taking the middle, then the four props.
		// What the scene asks for, not what it gets - the placement is bounded by
		// maxSceneInstances and reports if it had to drop any
		constexpr int sceneInstanceCount = 29;

		constexpr int cameraPresetCount = 6;
		constexpr int modelCount = 2;

		// Both models offer the same four now. The car also had a 50 voxel version, built so the
		// step past 40 could be judged rather than argued about - that question is settled and it
		// is no longer loaded. The file is still in assets if it is ever wanted again
		constexpr int maxDensityCount = 4;

		constexpr const char* modelNames[modelCount] = { "Shapes", "Car" };
		constexpr int densityCounts[modelCount] = { 4, 4 };

		constexpr const char* densityNames[modelCount][maxDensityCount] = {
			{ "40", "30", "20", "10" },
			{ "40", "30", "20", "10" }
		};

		// The same numbers again as values, because voxel size is what the renderer needs and a
		// label is what the panel needs
		constexpr int densityValues[modelCount][maxDensityCount] = {
			{ 40, 30, 20, 10 },
			{ 40, 30, 20, 10 }
		};

		constexpr const char* modelPaths[modelCount][maxDensityCount] = {
			{
				"assets/models/test/density_40.vox",
				"assets/models/test/density_30.vox",
				"assets/models/test/density_20.vox",
				"assets/models/test/density_10.vox"
			},
			{
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

		// Latched, so holding a key toggles once rather than once a frame
		bool Pressed(input::Key in_key, bool& inout_wasDown)
		{
			const bool down = input::IsKeyDown(in_key);
			const bool pressed = down && inout_wasDown == false;

			inout_wasDown = down;

			return pressed;
		}

		// Built on first use rather than at namespace scope, because the angles go through sin and
		// cos so they cannot be a constant expression
		const CameraPreset* CameraPresets()
		{
			static const CameraPreset presets[cameraPresetCount] = {
				{ "Gameplay",      "12 m of height in view, 30 deg",   MakeViewPreset(sceneTarget, 45.0f, -35.0f, 12.0f, 30.0f) },
				{ "Gameplay wide", "24 m of height in view, 30 deg",   MakeViewPreset(sceneTarget, 45.0f, -35.0f, 24.0f, 30.0f) },
				{ "Near ortho",    "12 m framed from far back, 6 deg", MakeViewPreset(sceneTarget, 45.0f, -35.0f, 12.0f,  6.0f) },
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
			{ mengerPath, mengerVoxelSize },
			{ patchPath, patchVoxelSize },
			{ tilePaths[0], patchVoxelSize },
			{ tilePaths[1], patchVoxelSize },
			{ tilePaths[2], patchVoxelSize }
		};

		// Recorded before anything is read rather than after, so a file that fails to parse is not
		// retried on every poll until it changes again
		for (int index = 0; index < sceneModelCount; ++index)
		{
			watched[index].path = sources[index].path;
			watched[index].loadedTime = 0;

			// A missing file is about to be reported by the import anyway
			(void)filesystem::GetModifiedTime(sources[index].path, watched[index].loadedTime);

			watched[index].lastSeenTime = watched[index].loadedTime;
		}

		watchedCount = sceneModelCount;

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


		// Ground first, then everything standing on it. Positions are hand placed against the
		// patch rather than derived, because the patch is a fixed mock-up and not a real scene
		int next = 0;

		// Every placement goes through this, so changing tileReach can only drop instances rather
		// than write past the array. The count then comes from what was actually placed instead of
		// from a constant that has to be kept in step by hand
		const auto place = [this, &next](const VoxelInstance& in_instance)
		{
			if (next < maxSceneInstances)
			{
				instances[next++] = in_instance;
			}
		};

		for (int gz = -tileReach; gz <= tileReach; ++gz)
		{
			for (int gx = -tileReach; gx <= tileReach; ++gx)
			{
				if (gx == 0 && gz == 0)
				{
					place({ .position = { 0.0f, patchBase, 0.0f }, .model = 3 });
					continue;
				}

				// Scattered rather than regular, so the trees and outcrops do not land on a grid
				const int scatter = (gx * 7 + gz * 13 + 64) % 11;
				const int tile = scatter < 2 ? 2 : (scatter < 5 ? 1 : 0);

				place({
					.position = { gx * tileSize, tileBase, gz * tileSize },
					.yaw = glm::radians(90.0f * (scatter % 4)),
					.model = 4 + tile
				});
			}
		}

		// On the path, which runs across the middle of the patch
		place({ .position = { 0.0f, 0.0f, 0.0f }, .yaw = glm::radians(5.0f), .model = 0 });

		// Stood off the end of the car, between it and the hut
		place({
			.position = { -2.2f, 0.0f, -0.9f },
			.yaw = glm::radians(20.0f),
			.model = 1
		});

		place({ .position = { 2.2f, 0.0f, -1.0f }, .model = 2 });

		// Turned and halved, because a cube is the one shape where both are obvious at a glance
		place({
			.position = { 2.6f, 0.0f, -2.0f },
			.yaw = glm::radians(25.0f),
			.scale = mengerSmallScale,
			.model = 2
		});

		instanceCount = next;

		if (next < sceneInstanceCount)
		{
			SOLAR_LOG_WARN(
				RENDER,
				"The scene wanted {} instances but only {} fit, so some of it is missing",
				sceneInstanceCount, next
			);
		}

		return Error::NONE;
	}
	void DebugClient::Update(Window& inout_window, Renderer& inout_renderer, float in_deltaTime)
	{
		// Typing "1" into a panel field used to toggle smooth normals as well. The latches are
		// still updated while a panel has the keyboard, so releasing a key over a text field does
		// not fire the toggle on the frame focus is lost
		const bool keyboardFree = input::IsUiCapturingKeyboard() == false;

		if (Pressed(input::Key::NUM_1, smoothNormalsKeyWasDown) && keyboardFree)
		{
			shading.smoothNormals = !shading.smoothNormals;
			SOLAR_LOG_INFO(RENDER, "Smooth normals {}", shading.smoothNormals ? "on" : "off");
		}

		if (Pressed(input::Key::NUM_2, shadowsKeyWasDown) && keyboardFree)
		{
			shading.shadows = !shading.shadows;
			SOLAR_LOG_INFO(RENDER, "Shadow rays {}", shading.shadows ? "on" : "off");
		}

		if (Pressed(input::Key::NUM_3, occlusionKeyWasDown) && keyboardFree)
		{
			shading.ambientOcclusion = !shading.ambientOcclusion;
			SOLAR_LOG_INFO(RENDER, "Ambient occlusion {}", shading.ambientOcclusion ? "on" : "off");
		}

		if (keyboardFree)
		{
			// Presets do not need latching because holding the key just keeps reapplying the same
			// camera. The rest are panel only
			if (input::IsKeyDown(input::Key::NUM_5)) { camera = CameraPresets()[0].camera; }
			if (input::IsKeyDown(input::Key::NUM_6)) { camera = CameraPresets()[1].camera; }

			// The same model resampled at several densities, so switching between them changes
			// voxels per metre and nothing else
			if (input::IsKeyDown(input::Key::NUM_7)) { densityIndex = 0; }
			if (input::IsKeyDown(input::Key::NUM_8)) { densityIndex = 1; }
			if (input::IsKeyDown(input::Key::NUM_9)) { densityIndex = 2; }
			if (input::IsKeyDown(input::Key::NUM_0)) { densityIndex = 3; }
		}

		UpdateFlyCamera(inout_window, in_deltaTime);
		UpdateSelection(inout_renderer);
		UpdateHotReload(inout_renderer, in_deltaTime);

		inout_renderer.SetShadingSettings(shading);
		inout_renderer.SetLightSettings(lights);
		inout_renderer.SetAtmosphereSettings(atmosphere);
	}
	void DebugClient::UpdateFlyCamera(Window& inout_window, float in_deltaTime)
	{
		// A look already underway continues regardless of what the UI thinks, because relative
		// mouse mode hides the cursor and ImGui's idea of where it is stops being meaningful.
		// Only starting one yields, which is what stops a right-drag on a panel grabbing the view
		const bool wantsLook = input::IsMouseButtonDown(input::MouseButton::RIGHT)
			&& (isLooking || input::IsUiCapturingMouse() == false);

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

		if (input::IsUiCapturingKeyboard() == false)
		{
			if (input::IsKeyDown(input::Key::W)) { move += basis.forward; }
			if (input::IsKeyDown(input::Key::S)) { move -= basis.forward; }
			if (input::IsKeyDown(input::Key::D)) { move += basis.right; }
			if (input::IsKeyDown(input::Key::A)) { move -= basis.right; }
			if (input::IsKeyDown(input::Key::E)) { move += basis.up; }
			if (input::IsKeyDown(input::Key::Q)) { move -= basis.up; }
		}

		// Normalising keeps diagonals the same speed as straight lines, and the zero check stops
		// us normalising a zero vector
		if (move != Vec3(0.0f))
		{
			camera.position += normalize(move) * speed * in_deltaTime;
		}
	}

	void DebugClient::UpdateHotReload(Renderer& inout_renderer, float in_deltaTime)
	{
		sinceLastPoll += in_deltaTime;

		if (sinceLastPoll < hotReloadInterval)
		{
			return;
		}

		sinceLastPoll = 0.0f;

		bool changed = false;

		for (int index = 0; index < watchedCount; ++index)
		{
			WatchedModel& model = watched[index];

			int64_t time = 0;

			if (filesystem::GetModifiedTime(model.path, time) != Error::NONE)
			{
				continue;
			}

			// Reloaded only once a changed file has stopped changing. A save is not atomic, so
			// acting on the first poll that notices one risks reading a half written file
			if (time != model.loadedTime && time == model.lastSeenTime)
			{
				changed = true;
			}

			model.lastSeenTime = time;
		}

		if (changed == false)
		{
			return;
		}

		SOLAR_LOG_INFO(CORE, "A model changed on disk, reloading the scene");

		// LoadScene rewrites the watch list before it reads anything, so a file that fails to
		// parse is reported once and then left alone until it is saved again. Nothing is uploaded
		// unless every model imported, so a bad save leaves the scene exactly as it was
		(void)LoadScene(inout_renderer);
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
