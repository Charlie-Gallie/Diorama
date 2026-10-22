#include "solar/core/Error.hpp"

namespace solar
{
	const char* ErrorToString(Error in_error)
	{
		// A switch rather than a lookup table: adding an enum value without a string
		// is a compiler warning here, instead of a silent out-of-bounds read.
		switch (in_error)
		{
		case Error::NONE:               return "No error";
		case Error::PLATFORM_FAILURE:   return "Platform failure";
		case Error::RENDERER_FAILURE:   return "Renderer failure";
		case Error::FILESYSTEM_FAILURE: return "Filesystem failure";
		case Error::INVALID_DATA:       return "Invalid data";
		}

		return "Unknown error";
	}
}
