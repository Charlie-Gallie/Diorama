#include "solar/platform/Window.hpp"

#include "solar/core/Log.hpp"

#include <SDL3/SDL.h>

namespace solar
{
	Window::Window(SDL_Window* in_handle) :
		handle(in_handle)
	{
	}

	Window::Window(Window&& in_other) noexcept :
		handle(in_other.handle)
	{
		in_other.handle = nullptr;
	}

	Window& Window::operator=(Window&& in_other) noexcept
	{
		if (this != &in_other)
		{
			if (handle != nullptr)
			{
				SDL_DestroyWindow(handle);
			}

			handle = in_other.handle;
			in_other.handle = nullptr;
		}

		return *this;
	}

	Window::~Window()
	{
		if (handle != nullptr)
		{
			SDL_DestroyWindow(handle);
		}
	}

		void Window::SetRelativeMouseMode(bool in_enabled)
	{
		if (SDL_SetWindowRelativeMouseMode(handle, in_enabled) == false)
		{
			SOLAR_LOG_WARN(PLATFORM, "Failed to set relative mouse mode: {}", SDL_GetError());
		}
	}

Error Window::Construct(const char* in_title, int32_t in_width, int32_t in_height)
	{
		handle = SDL_CreateWindow(
			in_title,
			static_cast<int>(in_width),
			static_cast<int>(in_height),
			SDL_WINDOW_RESIZABLE
		);

		if (handle == nullptr)
		{
			SOLAR_LOG_FATAL(
				PLATFORM,
				"Couldn't create SDL window: {}", SDL_GetError()
			);

			return solar::Error::PLATFORM_FAILURE;
		}

		SOLAR_LOG_INFO(PLATFORM, "Constructed window");
		return Error::NONE;
	}
}