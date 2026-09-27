#pragma once

#include "solar/core/Error.hpp"

/* Forward declarations */
struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;

namespace solar
{
	class Window;
	class ImGuiLayer;

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
		SDL_GPUGraphicsPipeline* graphicsPipeline = nullptr;
		bool isClaimed = false;

		// Using a pointer so we can properly handle its lifetime
		ImGuiLayer* imGuiLayer = nullptr;
	};
}
