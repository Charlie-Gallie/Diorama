#include "solar/core/Filesystem.hpp"

#include <string>

#include <SDL3/SDL.h>

namespace solar
{
	Error Filesystem::ReadFile(const char* in_path, Buffer& out_buffer)
	{
		// SDL can give the path of there the program is executed from which apparently std:: can't
		// (though the execution path works bit differently for macOS)
		static const char* basePath = SDL_GetBasePath();

		// SDL says "The returned path is guaranteed to end with a path separator" so no need to add it
		std::string fullPath = basePath + std::string(in_path);
		const char* fullPathCstr = fullPath.c_str();

		if (SDL_GetPathInfo(fullPathCstr, NULL) == false)
		{
			LogError(SDL_GetError());
			return Error::FILESYSTEM_FAILURE;
		}

		// Clear contents if anything is contained
		FreeBuffer(out_buffer);

		out_buffer.data = static_cast<unsigned char*>(
			SDL_LoadFile(fullPathCstr, &out_buffer.bytes)
		);

		if (out_buffer.data == NULL)
		{
			LogError(SDL_GetError());
			return Error::FILESYSTEM_FAILURE;
		}

		return Error::NONE;
	}

	void Filesystem::FreeBuffer(Buffer& inout_buffer)
	{
		// SDL does nothing if it's null
		SDL_free(static_cast<void*>(inout_buffer.data));

		inout_buffer.bytes = 0;
		inout_buffer.data = NULL; // Use NULL instead of nullptr because that's what SDL wants
	}
}