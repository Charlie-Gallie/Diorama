#include "Platform.hpp"

#include "solar/core/Log.hpp"

#include <SDL3/SDL.h>

namespace solar
{
	Platform::~Platform()
	{
		if (isInitialized)
		{
			SDL_Quit();
		}
	}

	Error Platform::Initialize(Subsystem in_subsystem)
	{
		SDL_InitFlags initFlags = 0;

		if (HasFlag(in_subsystem, Subsystem::VIDEO)) initFlags |= SDL_INIT_VIDEO;
		if (HasFlag(in_subsystem, Subsystem::AUDIO)) initFlags |= SDL_INIT_AUDIO;
		if (HasFlag(in_subsystem, Subsystem::GAMEPAD)) initFlags |= SDL_INIT_GAMEPAD;

		bool sdlInitialized = SDL_Init(initFlags);

		if (sdlInitialized == false)
		{
			LogError(SDL_GetError());

			return Error::PLATFORM_FAILURE;
		}

		isInitialized = true;

		return Error::NONE;
	}
}