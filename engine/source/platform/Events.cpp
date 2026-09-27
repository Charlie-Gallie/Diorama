#include "solar/platform/Events.hpp"

#include <SDL3/SDL.h>

namespace solar
{
	bool UpdateEvents()
	{
		bool isRunning = true;

		SDL_Event sdlEvent;
		while (SDL_PollEvent(&sdlEvent))
		{
			if (sdlEvent.type == SDL_EVENT_QUIT)
			{
				isRunning = false;
			}
		}

		return isRunning;
	}
}
