#pragma once

#include <string_view>

namespace solar
{
#if 0 // TODO: Add different log levels
	enum class LogLevel
	{
		INFO,
		WARN,
		ERR
	};
#endif

	void LogError(std::string_view in_text);
}
