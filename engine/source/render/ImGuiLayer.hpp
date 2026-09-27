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

		Error Initialize(
			SDL_GPUDevice* in_gpuDevice,
			SDL_Window* in_sdlWindow,
			SDL_GPUTextureFormat in_gpuTexFormat
		);

		void NewFrame();
		void PrepareDraw(SDL_GPUCommandBuffer* in_gpuCommandBuffer);
		void Draw(SDL_GPUCommandBuffer* in_gpuCommandBuffer, SDL_GPURenderPass* in_gpuRenderPass);

	private:
		ImGuiContext* context = nullptr;
	};
}