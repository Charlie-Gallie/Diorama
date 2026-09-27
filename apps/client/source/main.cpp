#include "solar/Engine.hpp"

#if SOLAR_DEBUG_TOOLS_ENABLED
#include "solar/devtools/DevTools.hpp"
#endif

int main()
{
	solar::EngineConfig config;
	config.windowTitle = "Diorama";
	config.windowWidth = 1280;
	config.windowHeight = 720;
#if SOLAR_DEBUG_TOOLS_ENABLED
	config.debugUi = solar::devtools::DrawAll;
#endif

	return solar::Run(config);
}
