#pragma once

#include "solar/core/DebugUiHook.hpp"
#include "solar/core/Error.hpp"

/* Forward declarations */
struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
enum SDL_GPUPresentMode;

namespace solar
{
	class Window;
	class ImGuiLayer;
	struct Camera;

	class Renderer
	{
	public:
		Renderer() = default;
		~Renderer();

		// Owns a GPU device handle, so it is neither copyable nor movable.
		// Declaring the destructor already suppresses the implicit moves.
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		[[nodiscard]] Error Initialize(Window& in_window, DebugUiCallback in_debugUiCallback = nullptr);

		void Render(const Camera& in_camera);
	private:
		SDL_Window* sdlWindow = nullptr;
		SDL_GPUDevice* gpuDevice = nullptr;
		SDL_GPUGraphicsPipeline* graphicsPipeline = nullptr;
		bool isClaimed = false;

		// Using a pointer so we can properly handle its lifetime
		ImGuiLayer* imGuiLayer = nullptr;
		DebugUiCallback debugUiCallback = nullptr;

		SDL_GPUPresentMode PickPresentMode();
	};
}
