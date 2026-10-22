#include "solar/core/Assert.hpp"

#include <SDL3/SDL.h>

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <windows.h>
#endif

#include <cstdlib>
#include <string>

namespace solar::assertion
{
	void Fail(const char* in_condition, const log::Location& in_location, std::string_view in_message)
	{
		const std::string text = std::format("Assertion failed: {} - {}", in_condition, in_message);

		// Fatal is flushed as it is written, so this survives the abort below
		log::Write(log::Category::CORE, log::Level::FATAL, in_location, text);

#if defined(_WIN32)
		if (IsDebuggerPresent())
		{
			__debugbreak();
		}
		else
#endif
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Assertion failed", text.c_str(), nullptr);
		}

		std::abort();
	}
}
