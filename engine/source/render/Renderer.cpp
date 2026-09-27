#include "solar/render/Renderer.hpp"

#include "ImGuiLayer.hpp"
#include "solar/core/Log.hpp"
#include "solar/platform/Window.hpp"
#include "solar/core/Filesystem.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <string>

namespace solar
{
	namespace
	{
		// Validation layers are expensive, so they are off in Shipping. NDEBUG is a
		// stand-in until the SE_ feature flags exist: Debug and Development strip it,
		// Shipping defines it.
#ifdef NDEBUG
		constexpr bool enableGpuDebug = false;
#else
		constexpr bool enableGpuDebug = true;
#endif

		SDL_GPUShader* LoadShader(SDL_GPUDevice* in_gpuDevice, const char* in_path, SDL_GPUShaderStage in_shaderStage)
		{
			Filesystem::Buffer buffer;
			
			if (Filesystem::ReadFile(in_path, buffer) != Error::NONE)
			{
				LogError(SDL_GetError());
				return nullptr;
			}

			SDL_GPUShaderCreateInfo info{};
			info.code = buffer.data;
			info.code_size = buffer.bytes;
			info.entrypoint = "main"; // matches -E main
			info.format = SDL_GPU_SHADERFORMAT_SPIRV;
			info.stage = in_shaderStage;
			info.num_samplers = 0;
			info.num_uniform_buffers = 0;

			SDL_GPUShader* gpuShader = SDL_CreateGPUShader(in_gpuDevice, &info);
			if (gpuShader == NULL)
			{
				LogError(SDL_GetError());
				return nullptr;
			}

			return gpuShader;
		}
	}

	Renderer::~Renderer()
	{
		// We explicitly delete imGuiLayer here before anything else because
		// it uses resources held by this object
		delete imGuiLayer;
		imGuiLayer = nullptr;

		if (graphicsPipeline != nullptr)
		{
			SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
		}

		if (isClaimed)
		{
			SDL_ReleaseWindowFromGPUDevice(gpuDevice, sdlWindow);
		}

		if (gpuDevice != nullptr)
		{
			SDL_DestroyGPUDevice(gpuDevice);
		}
	}

	Error Renderer::Initialize(Window& in_window)
	{
		sdlWindow = in_window.GetHandle();

		// TODO: advertise DXIL and MSL once the shader toolchain produces them.
		// Only offering SPIR-V means SDL will always select Vulkan, including on
		// Windows where D3D12 would be the better default.
		gpuDevice = SDL_CreateGPUDevice(
			SDL_GPU_SHADERFORMAT_SPIRV,
			enableGpuDebug,
			nullptr
		);

		if (gpuDevice == nullptr)
		{
			LogError(SDL_GetError());
			return Error::RENDERER_FAILURE;
		}

		if (SDL_ClaimWindowForGPUDevice(gpuDevice, sdlWindow) == false)
		{
			LogError(SDL_GetError());
			return Error::RENDERER_FAILURE;
		}

		isClaimed = true;

		const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(gpuDevice, sdlWindow);

		imGuiLayer = new ImGuiLayer();
		if (imGuiLayer->Initialize(gpuDevice, sdlWindow, swapchainFormat) != Error::NONE)
		{
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUShader* vertShader = LoadShader(
			gpuDevice,
			"shaders/Fullscreen.vert.spv",
			SDL_GPU_SHADERSTAGE_VERTEX
		);

		if (vertShader == nullptr)
		{
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUShader* fragShader = LoadShader(
			gpuDevice,
			"shaders/Fullscreen.frag.spv",
			SDL_GPU_SHADERSTAGE_FRAGMENT
		);

		if (fragShader == nullptr)
		{
			SDL_ReleaseGPUShader(gpuDevice, vertShader);
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUColorTargetDescription colorTarget{};
		colorTarget.format = swapchainFormat;

		SDL_GPUGraphicsPipelineCreateInfo info{};
		info.vertex_shader = vertShader;
		info.fragment_shader = fragShader;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

		info.target_info.color_target_descriptions = &colorTarget;
		info.target_info.num_color_targets = 1;

		graphicsPipeline = SDL_CreateGPUGraphicsPipeline(gpuDevice, &info);

		// Release shaders whether creating the graphics pipeline was successful or not
		SDL_ReleaseGPUShader(gpuDevice, vertShader);
		SDL_ReleaseGPUShader(gpuDevice, fragShader);

		if (graphicsPipeline == nullptr)
		{
			LogError(SDL_GetError());
			return Error::RENDERER_FAILURE;
		}

		return Error::NONE;
	}

	void Renderer::Render()
	{
		auto frameStartTime = std::chrono::steady_clock::now();

		SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);

		if (commandBuffer == nullptr)
		{
			LogError(SDL_GetError());
			return;
		}

		SDL_GPUTexture* swapchainTexture = nullptr;

		if (SDL_WaitAndAcquireGPUSwapchainTexture(
				commandBuffer,
				sdlWindow,
				&swapchainTexture,
				nullptr,
				nullptr
			) == false
		) {
			LogError(SDL_GetError());

			// The command buffer has been acquired, so it must be disposed of either
			// way or it leaks.
			SDL_CancelGPUCommandBuffer(commandBuffer);
			return;
		}

		if (swapchainTexture == nullptr) // Minimised or occluded - not an error
		{
			SDL_SubmitGPUCommandBuffer(commandBuffer);
			return;
		}

		imGuiLayer->NewFrame();
		ImGui::ShowDemoWindow();
		imGuiLayer->PrepareDraw(commandBuffer);

		SDL_GPUColorTargetInfo target{};
		target.texture = swapchainTexture;
		target.clear_color = { 0.5f, 0.1f, 0.7f, 1.0f };
		target.load_op = SDL_GPU_LOADOP_CLEAR;
		target.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);

		SDL_BindGPUGraphicsPipeline(renderPass, graphicsPipeline);
		SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);

		imGuiLayer->Draw(commandBuffer, renderPass);

		auto frameEndTime = std::chrono::steady_clock::now();
		std::chrono::nanoseconds frameTimeNs = frameEndTime - frameStartTime;
		float frameTimeMs = frameTimeNs / std::chrono::milliseconds(1);
		//SDL_SetWindowTitle(sdlWindow, std::to_string(frameTimeMs).c_str());

		SDL_EndGPURenderPass(renderPass);
		SDL_SubmitGPUCommandBuffer(commandBuffer);
	}
}
