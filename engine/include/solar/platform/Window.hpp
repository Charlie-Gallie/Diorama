#pragma once

#include "solar/core/Error.hpp"

#include <cstdint>

struct SDL_Window; // Forward declaration

namespace solar
{
	class Window
	{
	public:
		Window() = default;
		~Window();

		Window(Window&& in_other) noexcept;
		Window& operator=(Window&& in_other) noexcept;

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		[[nodiscard]] Error Construct(const char* in_title, int32_t in_width, int32_t in_height);

		// Returning a pointer to an incomplete type keeps SDL out of this header.
		inline SDL_Window* GetHandle() const { return handle; }
	private:
		explicit Window(SDL_Window* in_handle);
		SDL_Window* handle = nullptr;
	};
}