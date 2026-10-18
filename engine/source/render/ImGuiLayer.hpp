#pragma once

#include <solar/core/Error.hpp>

#include <imgui.h>
#include <SDL3/SDL.h>

namespace solar
{
	class ImGuiLayer
	{
	public:
		ImGuiLayer() = default;
		~ImGuiLayer();

		ImGuiLayer(const ImGuiLayer&) = delete;
		ImGuiLayer& operator=(const ImGuiLayer&) = delete;

		[[nodiscard]] Error Initialize(
			SDL_GPUDevice* in_gpuDevice,
			SDL_Window* in_sdlWindow,
			SDL_GPUTextureFormat in_gpuTexFormat,
			SDL_GPUPresentMode in_presentMode
		);

		void NewFrame();
		void PrepareDraw(SDL_GPUCommandBuffer* in_gpuCommandBuffer);
		void Draw(SDL_GPUCommandBuffer* in_gpuCommandBuffer, SDL_GPURenderPass* in_gpuRenderPass);

	private:
		ImGuiContext* context = nullptr;
		SDL_Window* sdlWindow = nullptr;

		// One flag per backend rather than one for the pair. Initialize can fail between the two,
		// and shutting down a backend that never started is not something ImGui tolerates
		bool isPlatformStarted = false;
		bool isRendererStarted = false;
	};
}