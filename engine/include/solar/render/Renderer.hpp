#pragma once

#include "solar/core/Error.hpp"

/* Forward declarations */
struct SDL_Window;
struct SDL_GPUDevice;

namespace solar
{
	class Window;

	class Renderer
	{
	public:
		Renderer() = default;
		~Renderer();

		// Owns a GPU device handle, so it is neither copyable nor movable.
		// Declaring the destructor already suppresses the implicit moves.
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		[[nodiscard]] Error Initialize(Window& in_window);

		void Render();
	private:
		SDL_Window* sdlWindow = nullptr;
		SDL_GPUDevice* gpuDevice = nullptr;
		bool isClaimed = false;
	};
}
