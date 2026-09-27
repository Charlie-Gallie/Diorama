#include "solar/platform/Events.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

namespace solar
{
	bool UpdateEvents()
	{
		bool isRunning = true;

		SDL_Event sdlEvent;
		while (SDL_PollEvent(&sdlEvent))
		{
			ImGui_ImplSDL3_ProcessEvent(&sdlEvent);

			if (sdlEvent.type == SDL_EVENT_QUIT)
			{
				isRunning = false;
			}
		}

		return isRunning;
	}
}
