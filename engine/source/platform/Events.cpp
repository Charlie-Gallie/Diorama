#include "solar/platform/Events.hpp"

#include "platform/EventObservers.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

namespace solar
{
	namespace
	{
		std::vector<EventObserver>& Observers()
		{
			static std::vector<EventObserver> observers;
			return observers;
		}
	}

	void AddEventObserver(EventObserver in_observer)
	{
		Observers().push_back(in_observer);
	}

	void RemoveEventObserver(EventObserver in_observer)
	{
		std::vector<EventObserver>& observers = Observers();
		observers.erase(std::remove(observers.begin(), observers.end(), in_observer), observers.end());
	}

	bool UpdateEvents()
	{
		bool isRunning = true;

		SDL_Event sdlEvent;
		while (SDL_PollEvent(&sdlEvent))
		{
			for (const EventObserver observer : Observers())
			{
				observer(sdlEvent);
			}

			if (sdlEvent.type == SDL_EVENT_QUIT)
			{
				isRunning = false;
			}
		}

		return isRunning;
	}
}
