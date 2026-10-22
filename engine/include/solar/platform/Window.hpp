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

		// Hides the cursor and reports motion as deltas rather than a position,
		// so looking around is not bounded by the edges of the screen.
		void SetRelativeMouseMode(bool in_enabled);

		bool IsMinimised() const;

		// Blocks while the window is minimised. Returns false if a quit arrived while waiting,
		// which is left queued for the event loop to see as well
		[[nodiscard]] bool WaitUntilRestored();

	private:
		explicit Window(SDL_Window* in_handle);
		SDL_Window* handle = nullptr;
	};
}
