#include "ImGuiLayer.hpp"

#include <imgui.h>
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"

#include "solar/core/Log.hpp"

namespace solar
{
	ImGuiLayer::~ImGuiLayer()
	{
		// Initialize may never have run, or may have failed part way through.
		if (context == nullptr)
		{
			return;
		}

		ImGui_ImplSDLGPU3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext(context);

		context = nullptr;
	}

	Error ImGuiLayer::Initialize(
		SDL_GPUDevice* in_gpuDevice,
		SDL_Window* in_sdlWindow,
		SDL_GPUTextureFormat in_gpuTexFormat,
		SDL_GPUPresentMode in_presentMode
	) {
		sdlWindow = in_sdlWindow;

		IMGUI_CHECKVERSION();

		context = ImGui::CreateContext();
		ImGui::StyleColorsDark();
		if (ImGui_ImplSDL3_InitForSDLGPU(sdlWindow) == false)
		{
			SOLAR_LOG_ERR(
				RENDER,
				"Failed ImGui_ImplSDL3_InitForSDLGPU(...)"
			);
			return Error::RENDERER_FAILURE;
		}

		ImGui_ImplSDLGPU3_InitInfo info{};
		info.ColorTargetFormat = in_gpuTexFormat;
		info.Device = in_gpuDevice;
		info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
		info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
		info.PresentMode = in_presentMode;

		if (ImGui_ImplSDLGPU3_Init(&info) == false)
		{
			SOLAR_LOG_ERR(
				RENDER,
				"Failed ImGui_ImplSDLGPU3_Init(...)"
			);
			return Error::RENDERER_FAILURE;
		}

		return Error::NONE;
	}

	void ImGuiLayer::NewFrame()
	{
		ImGui_ImplSDLGPU3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		ImGuiStyle& style = ImGui::GetStyle();

		float displayScale = SDL_GetWindowDisplayScale(sdlWindow);
		style.FontScaleDpi = SDL_floorf(displayScale + 0.5f); // Prevent weird font rasterization by using whole number as scale
	}

	void ImGuiLayer::PrepareDraw(SDL_GPUCommandBuffer* in_gpuCommandBuffer)
	{
		ImGui::Render();
		ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), in_gpuCommandBuffer);
	}

	void ImGuiLayer::Draw(SDL_GPUCommandBuffer* in_gpuCommandBuffer, SDL_GPURenderPass* in_gpuRenderPass)
	{
		ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), in_gpuCommandBuffer, in_gpuRenderPass);
	}
}