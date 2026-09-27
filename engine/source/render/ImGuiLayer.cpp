#include "ImGuiLayer.hpp"

#include <imgui.h>
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"

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
		SDL_GPUTextureFormat in_gpuTexFormat
	) {
		IMGUI_CHECKVERSION();

		context = ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ImGui_ImplSDL3_InitForSDLGPU(in_sdlWindow);

		ImGui_ImplSDLGPU3_InitInfo info{};
		info.ColorTargetFormat = in_gpuTexFormat;
		info.Device = in_gpuDevice;
		info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
		info.PresentMode = SDL_GPU_PRESENTMODE_IMMEDIATE; // TODO: Apparently IMMEDIATE may not always be supported
		info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;

		ImGui_ImplSDLGPU3_Init(&info); // TODO: Need to check the result? Seems to always be true

		return Error::NONE;
	}

	void ImGuiLayer::NewFrame()
	{
		ImGui_ImplSDLGPU3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
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