#include "solar/Engine.hpp"

#include "platform/Platform.hpp"
#include "solar/core/Log.hpp"
#include "solar/core/DebugUiHook.hpp"
#include "solar/platform/Events.hpp"
#include "solar/platform/Input.hpp"
#include "solar/platform/Window.hpp"
#include "solar/render/Camera.hpp"
#include "solar/render/Renderer.hpp"
#include "solar/render/ShadingSettings.hpp"

#include <chrono>

namespace solar
{
	namespace
	{
		// The simulation always advances in whole steps of this size, so physics and
		// (later) network prediction stay deterministic regardless of frame rate.
		constexpr double fixedDeltaTime = 1.0 / 60.0;

		// One long stall must not queue up hundreds of catch-up steps, which would
		// take even longer to run - the "spiral of death". Dropping simulation time
		// makes the game briefly run slow instead of freezing permanently.
		constexpr double maxFrameTime = 0.25;

		// Radians per pixel of mouse motion.
		constexpr float lookSensitivity = 0.0025f;

		constexpr float minPitch = glm::radians(-89.9f);
		constexpr float maxPitch = glm::radians(89.9f);

		// TODO: all of the following belongs to the client, as a debug fly camera
		// and a model browser. It lives here because neither exists yet.

		// The scene shader normalises a model so its longest axis spans -1..1, which
		// makes world units depend on what is loaded. The density_*.vox scenes are
		// 6.2 m along that axis, putting one unit at 3.1 m.
		constexpr float metresPerUnit = 3.1f;

		constexpr float walkSpeedMetres = 4.0f;
		constexpr float fastSpeedMetres = 16.0f;

		// Roughly the middle of the test scene, a little above the floor
		constexpr Vec3 sceneTarget = { 0.0f, -0.11f, 0.0f };

		// The blue sphere on the front row, for looking at one object closely
		constexpr Vec3 sphereTarget = { -0.74f, -0.11f, 0.24f };

		// Presets are described by the vertical extent they frame, in metres, which
		// is the thing actually being chosen - the distance follows from it and the
		// field of view. A narrow field of view at a long distance is how the
		// near-orthographic look is reached without an orthographic projection.
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

		constexpr int cameraPresetCount = 6;
		constexpr int modelCount = 2;

		// Each model carries its own densities, because a .vox axis cannot exceed
		// 256: the shapes scene is 6.2 m along its longest axis, which rules out
		// 50 voxels per metre, while the car at 4.3 m has room for it.
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
	}

	namespace
	{
		// RAII so the early failure returns below cannot skip Shutdown.
		struct LogScope
		{
			LogScope() { log::Initialize(); }
			~LogScope() { log::Shutdown(); }

			LogScope(const LogScope&) = delete;
			LogScope& operator=(const LogScope&) = delete;
		};
	}

	int Run(const EngineConfig& in_config)
	{
		LogScope logScope;

		Platform platform;
		if (platform.Initialize(Platform::Subsystem::VIDEO) != Error::NONE)
		{
			SOLAR_LOG_FATAL(PLATFORM, "Failed to initialize platform");
			return 1;
		}

		Window window;
		if (window.Construct(
			in_config.windowTitle,
			in_config.windowWidth,
			in_config.windowHeight
		) != Error::NONE
		) {
			SOLAR_LOG_FATAL(PLATFORM, "Failed to initialize window");
			return 1;
		}

		Renderer renderer;
		if (renderer.Initialize(window, in_config.debugUi) != Error::NONE)
		{
			SOLAR_LOG_FATAL(RENDER, "Failed to initialize renderer");
			return 1;
		}

		const CameraPreset cameraPresets[cameraPresetCount] = {
			{ "Gameplay",      "9 m of height in view, 30 deg",   MakeViewPreset(sceneTarget,  45.0f, -35.0f,  9.0f, 30.0f) },
			{ "Gameplay wide", "18 m of height in view, 30 deg",  MakeViewPreset(sceneTarget,  45.0f, -35.0f, 18.0f, 30.0f) },
			{ "Near ortho",    "9 m framed from far back, 6 deg", MakeViewPreset(sceneTarget,  45.0f, -35.0f,  9.0f,  6.0f) },
			{ "Steep",         "7 m, pitched down for indoors",   MakeViewPreset(sceneTarget,  45.0f, -58.0f,  7.0f, 30.0f) },
			{ "Prop close-up", "2 m, on the blue sphere",         MakeViewPreset(sphereTarget, 25.0f, -18.0f,  2.0f, 35.0f) },
			{ "Front",         "5 m, flat on for silhouettes",    MakeViewPreset(sceneTarget,   0.0f,  -8.0f,  5.0f, 25.0f) }
		};

		Camera camera = cameraPresets[0].camera;
		bool isLooking = false;

		ShadingSettings shading;
		int modelIndex = 0;
		int densityIndex = 0;
		int appliedModelIndex = 0;
		int appliedDensityIndex = 0;

		bool smoothNormalsKeyWasDown = false;

		DebugUiContext debugUi;
		debugUi.camera = &camera;
		debugUi.shading = &shading;
		debugUi.densityIndex = &densityIndex;
		debugUi.cameraPresets = cameraPresets;
		debugUi.cameraPresetCount = cameraPresetCount;
		debugUi.modelIndex = &modelIndex;
		debugUi.modelNames = modelNames;
		debugUi.modelCount = modelCount;
		debugUi.metresPerUnit = metresPerUnit;

		// steady_clock, not system_clock: the latter can jump when the OS syncs time.
		auto previousTime = std::chrono::steady_clock::now();
		double accumulator = 0.0;

		bool isRunning = true;
		while (isRunning)
		{
			const auto currentTime = std::chrono::steady_clock::now();
			double frameTime = std::chrono::duration<double>(currentTime - previousTime).count();
			previousTime = currentTime;

			if (frameTime > maxFrameTime)
			{
				frameTime = maxFrameTime;
			}

			accumulator += frameTime;

			isRunning = UpdateEvents();

			while (accumulator >= fixedDeltaTime)
			{
				// TODO: FixedUpdate(fixedDeltaTime)
				accumulator -= fixedDeltaTime;
			}

			// TODO: belongs to the client, as a debug fly camera
			{
				// Latched, so holding the key toggles once rather than every frame
				const bool smoothNormalsKeyDown = input::IsKeyDown(input::Key::NUM_1);

				if (smoothNormalsKeyDown && smoothNormalsKeyWasDown == false)
				{
					shading.smoothNormals = !shading.smoothNormals;
					SOLAR_LOG_INFO(RENDER, "Smooth normals {}", shading.smoothNormals ? "on" : "off");
				}

				smoothNormalsKeyWasDown = smoothNormalsKeyDown;

				// Presets need no latching: holding the key just keeps reapplying
				// the same camera, which is harmless. The rest are panel only.
				if (input::IsKeyDown(input::Key::NUM_5)) { camera = cameraPresets[0].camera; }
				if (input::IsKeyDown(input::Key::NUM_6)) { camera = cameraPresets[1].camera; }

				// The same model resampled at several densities, so switching between
				// them changes voxels per metre and nothing else.
				if (input::IsKeyDown(input::Key::NUM_7)) { densityIndex = 0; }
				if (input::IsKeyDown(input::Key::NUM_8)) { densityIndex = 1; }
				if (input::IsKeyDown(input::Key::NUM_9)) { densityIndex = 2; }
				if (input::IsKeyDown(input::Key::NUM_0)) { densityIndex = 3; }

				const bool wantsLook = input::IsMouseButtonDown(input::MouseButton::RIGHT);

				if (wantsLook != isLooking)
				{
					isLooking = wantsLook;
					window.SetRelativeMouseMode(isLooking);
				}

				// Consumed every frame whether looking or not, so motion while the
				// button was up does not arrive as a jump when it goes down.
				const Vec2 mouseDelta = input::ConsumeMouseDelta();

				if (isLooking)
				{
					camera.yaw += mouseDelta.x * lookSensitivity;
					camera.pitch -= mouseDelta.y * lookSensitivity;

					// At exactly +-90 degrees forward is parallel to world up and
					// the cross product that produces right collapses to zero.
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

				// Normalising keeps diagonals the same speed as straight lines,
				// and the zero check avoids normalising a zero vector.
				if (move != Vec3(0.0f))
				{
					camera.position += normalize(move) * speed * static_cast<float>(frameTime);
				}
			}

			// Either the key above or the debug panel may have moved these on, so
			// they are applied here rather than at the point of change.
			renderer.SetShadingSettings(shading);

			// Models do not all offer the same densities, so the panel is told which
			// list applies before it draws and the index is kept inside it.
			debugUi.densityNames = densityNames[modelIndex];
			debugUi.densityCount = densityCounts[modelIndex];

			densityIndex = glm::clamp(densityIndex, 0, densityCounts[modelIndex] - 1);

			if (densityIndex != appliedDensityIndex || modelIndex != appliedModelIndex)
			{
				const char* const path = modelPaths[modelIndex][densityIndex];

				if (renderer.SetModel(path) == Error::NONE)
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

			// TODO: pass accumulator / fixedDeltaTime as the interpolation alpha once
			// there is anything with a previous and current transform to blend.
			renderer.Render(camera, debugUi);
		}

		return 0;
	}
}
