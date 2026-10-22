#pragma once

#include <SDL3/SDL.h>

namespace solar
{
	// Lets something above the platform layer see every event as it is polled, without the platform
	// layer knowing who is listening. Private to the engine, because the events are SDL's
	using EventObserver = void (*)(const SDL_Event& in_event);

	void AddEventObserver(EventObserver in_observer);
	void RemoveEventObserver(EventObserver in_observer);
}
