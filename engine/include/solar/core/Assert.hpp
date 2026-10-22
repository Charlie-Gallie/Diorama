#pragma once

#include "solar/core/Log.hpp"

#include <format>
#include <string_view>

// For programmer errors only. Anything that can arrive from a file, the network or a script is
// validated and returns an Error instead, because a modder's mistake must not stop the game
// Compiled out in Shipping, so the condition and message are never evaluated there
#if SOLAR_ASSERTS_ENABLED
	#define SOLAR_ASSERT(condition, ...) \
		do \
		{ \
			if (!(condition)) \
			{ \
				solar::assertion::Fail(#condition, SOLAR_LOG_LOCATION, std::format(__VA_ARGS__)); \
			} \
		} while (false)
#else
	#define SOLAR_ASSERT(condition, ...) ((void)0)
#endif

// Not solar::assert, which the assert macro from <cassert> would expand inside
namespace solar::assertion
{
	// Logs, breaks into an attached debugger, otherwise shows a message box, then aborts
	[[noreturn]] void Fail(const char* in_condition, const log::Location& in_location, std::string_view in_message);
}
