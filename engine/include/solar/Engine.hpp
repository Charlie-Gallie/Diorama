#pragma once

#include <cstdint>

#include "solar/core/DebugUiHook.hpp"

namespace solar
{
	struct EngineConfig
	{
		const char* windowTitle = "Solar Engine";
		int32_t windowWidth = 1280;
		int32_t windowHeight = 720;
		DebugUiCallback debugUi = nullptr;
	};

	int Run(const EngineConfig& in_config);
}