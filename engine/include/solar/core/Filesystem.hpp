#pragma once

#include <cstddef>
#include <cstdint>

#include "solar/core/Error.hpp"
#include "solar/core/Log.hpp"

namespace solar {
namespace filesystem {
	struct Buffer;

	// The contents of the buffer will be freed (if required)
	Error ReadFile(const char* in_path, Buffer& out_buffer);

	// Nanoseconds since the epoch, for noticing that a file has been written since it was read.
	// Resolved exactly as ReadFile resolves it, so a watcher cannot end up watching one file
	// while the loader reads another
	[[nodiscard]] Error GetModifiedTime(const char* in_path, int64_t& out_time);

	void FreeBuffer(Buffer& inout_buffer);

	struct Buffer
	{
		unsigned char* data = nullptr;
		size_t bytes = 0;

		~Buffer()
		{
			FreeBuffer(*this);
		}

		Buffer() = default;
		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		// The source is emptied field by field rather than through FreeBuffer, because that
		// frees - and what it would free is the allocation this object has just taken over
		Buffer(Buffer&& in_other) noexcept
		{
			data = in_other.data;
			bytes = in_other.bytes;

			in_other.data = nullptr;
			in_other.bytes = 0;
		}

		Buffer& operator=(Buffer&& in_other) noexcept
		{
			if (this != &in_other)
			{
				// Whatever this already held has to go before it is overwritten
				FreeBuffer(*this);

				data = in_other.data;
				bytes = in_other.bytes;

				in_other.data = nullptr;
				in_other.bytes = 0;
			}

			return *this;
		}
	};
} } // namespace solar and filesystem