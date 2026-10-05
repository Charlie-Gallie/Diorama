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

		Buffer(Buffer&& in_other) noexcept
		{
			data = in_other.data;
			bytes = in_other.bytes;

			FreeBuffer(in_other);
		}

		Buffer& operator=(Buffer&& in_other) noexcept
		{
			if (this != &in_other)
			{
				data = in_other.data;
				bytes = in_other.bytes;

				FreeBuffer(in_other);
			}

			return *this;
		}
	};
} } // namespace solar and filesystem