#pragma once

#include <cstddef>
#include <cstdint>

#include "solar/core/Error.hpp"
#include "solar/core/Log.hpp"

namespace solar {
namespace Filesystem {
	struct Buffer;

	// The contents of the buffer will be freed (if required)
	Error ReadFile(const char* in_path, Buffer& out_buffer);

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