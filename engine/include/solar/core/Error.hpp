#pragma once

namespace solar
{
	enum class Error
	{
		NONE = 0,
		PLATFORM_FAILURE,
		RENDERER_FAILURE,
		FILESYSTEM_FAILURE
	};

	const char* ErrorToString(Error in_error);
}

// #define SOLAR_ASSERT