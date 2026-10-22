#include "solar/core/Filesystem.hpp"

#include <string>

#include <SDL3/SDL.h>

namespace solar
{
	namespace
	{
		// Dev builds look in the source tree first, so a model saved in MagicaVoxel is picked up by a
		// running game rather than waiting for the next build to copy it across. Shipping has no
		// source tree, so SOLAR_SOURCE_DIR is not defined there and this falls straight through.
		// Anything the source tree does not have - compiled shaders, for one - falls through too
		std::string ResolvePath(const char* in_path)
		{
			// SDL can give the path of where the program is executed from which apparently std:: can't
			// (though the execution path works bit differently for macOS).
			// SDL says "The returned path is guaranteed to end with a path separator" so no need to add it
			static const char* basePath = SDL_GetBasePath();

#ifdef SOLAR_SOURCE_DIR
			const std::string fromSource = SOLAR_SOURCE_DIR + std::string(in_path);

			SDL_PathInfo info{};

			if (SDL_GetPathInfo(fromSource.c_str(), &info))
			{
				return fromSource;
			}
#endif

			return basePath + std::string(in_path);
		}
	}

	Error filesystem::ReadFile(const char* in_path, Buffer& out_buffer)
	{
		const std::string fullPath = ResolvePath(in_path);
		const char* fullPathCstr = fullPath.c_str();

		// Clear contents if anything is contained
		FreeBuffer(out_buffer);

		out_buffer.data = static_cast<unsigned char*>(
			SDL_LoadFile(fullPathCstr, &out_buffer.bytes)
		);

		if (out_buffer.data == NULL)
		{
			SOLAR_LOG_ERR(
				CORE,
				"Failed to read file \"{}\": {}", fullPathCstr, SDL_GetError()
			);
			return Error::FILESYSTEM_FAILURE;
		}

		return Error::NONE;
	}

	Error filesystem::GetModifiedTime(const char* in_path, int64_t& out_time)
	{
		const std::string fullPath = ResolvePath(in_path);

		SDL_PathInfo info{};

		if (SDL_GetPathInfo(fullPath.c_str(), &info) == false)
		{
			// Not logged. A watcher asks this of every file every poll, and whether a missing file
			// matters is the caller's judgement rather than this function's
			return Error::FILESYSTEM_FAILURE;
		}

		out_time = static_cast<int64_t>(info.modify_time);

		return Error::NONE;
	}

	void filesystem::FreeBuffer(Buffer& inout_buffer)
	{
		// SDL does nothing if it's null
		SDL_free(static_cast<void*>(inout_buffer.data));

		inout_buffer.bytes = 0;
		inout_buffer.data = nullptr;
	}
}
