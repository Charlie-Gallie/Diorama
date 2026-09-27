#include "solar/Engine.hpp"

#include "platform/Platform.hpp"
#include "solar/core/Log.hpp"
#include "solar/core/DebugUiHook.hpp"
#include "solar/platform/Events.hpp"
#include "solar/platform/Window.hpp"
#include "solar/render/Camera.hpp"
#include "solar/render/Renderer.hpp"

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

		// TODO: belongs to the client
		Camera camera;

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

			// TODO: pass accumulator / fixedDeltaTime as the interpolation alpha once
			// there is anything with a previous and current transform to blend.
			renderer.Render(camera);
		}

		return 0;
	}
}
