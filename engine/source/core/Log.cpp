#include "solar/core/Log.hpp"

#include <SDL3/SDL.h>

namespace solar
{
	void LogError(std::string_view in_text)
	{
		// The text is passed as an argument, never as the format string: a message
		// containing % would otherwise read arguments that were never passed.
		SDL_Log("%.*s", static_cast<int>(in_text.size()), in_text.data());
	}
}
