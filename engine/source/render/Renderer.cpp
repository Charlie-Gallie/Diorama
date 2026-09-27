#include "solar/render/Renderer.hpp"

#include "CameraData.hpp"
#include "ImGuiLayer.hpp"
#include "solar/core/Log.hpp"
#include "solar/platform/Window.hpp"
#include "solar/core/Filesystem.hpp"

#include <SDL3/SDL.h>

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

		SDL_GPUShader* LoadShader(SDL_GPUDevice* in_gpuDevice, const char* in_path, SDL_GPUShaderStage in_shaderStage, Uint32 in_numUniformBuffers)
		{
			filesystem::Buffer buffer;
			
			if (filesystem::ReadFile(in_path, buffer) != Error::NONE)
			{
				SOLAR_LOG_ERR(
					RENDER,
					"Failed to load shader file \"{}\": {}", in_path, SDL_GetError()
				);
				return nullptr;
			}

			SDL_GPUShaderCreateInfo info{};
			info.code = buffer.data;
			info.code_size = buffer.bytes;
			info.entrypoint = "main"; // matches -E main
			info.format = SDL_GPU_SHADERFORMAT_SPIRV;
			info.stage = in_shaderStage;
			info.num_samplers = 0;
			info.num_uniform_buffers = in_numUniformBuffers;

			SDL_GPUShader* gpuShader = SDL_CreateGPUShader(in_gpuDevice, &info);
			if (gpuShader == NULL)
			{
				SOLAR_LOG_ERR(
					RENDER,
					"Failed to create GPU shader: {}", SDL_GetError()
				);
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

	Error Renderer::Initialize(Window& in_window, DebugUiCallback in_debugUiCallback)
	{
		sdlWindow = in_window.GetHandle();
		debugUiCallback = in_debugUiCallback;

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
			SOLAR_LOG_ERR(
				RENDER,
				"Failed to create GPU device: {}", SDL_GetError()
			);
			return Error::RENDERER_FAILURE;
		}

		if (SDL_ClaimWindowForGPUDevice(gpuDevice, sdlWindow) == false)
		{
			SOLAR_LOG_ERR(
				RENDER,
				"Failed to claim window for GPU device: {}", SDL_GetError()
			);
			return Error::RENDERER_FAILURE;
		}

		isClaimed = true;

		SDL_GPUPresentMode presentMode = PickPresentMode();

		if (SDL_SetGPUSwapchainParameters(
			gpuDevice,
			sdlWindow,
			SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
			presentMode
		) == false)
		{
			SOLAR_LOG_ERR(
				RENDER,
				"Failed to set swapchain parameters: {}", SDL_GetError()
			);
		}

		const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(gpuDevice, sdlWindow);

		imGuiLayer = new ImGuiLayer();
		if (imGuiLayer->Initialize(gpuDevice, sdlWindow, swapchainFormat, presentMode) != Error::NONE)
		{
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUShader* vertShader = LoadShader(
			gpuDevice,
			"shaders/Fullscreen.vert.spv",
			SDL_GPU_SHADERSTAGE_VERTEX,
			0
		);

		if (vertShader == nullptr)
		{
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUShader* fragShader = LoadShader(
			gpuDevice,
			"shaders/Fullscreen.frag.spv",
			SDL_GPU_SHADERSTAGE_FRAGMENT,
			1
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
			SOLAR_LOG_ERR(
				RENDER,
				"Failed to create graphics pipeline: {}", SDL_GetError()
			);
			return Error::RENDERER_FAILURE;
		}

		SOLAR_LOG_INFO(RENDER, "Initialized renderer");
		return Error::NONE;
	}

	void Renderer::Render(const Camera& in_camera)
	{
		SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);

		if (commandBuffer == nullptr)
		{
			SOLAR_LOG_ERR(
				RENDER,
				"Failed to acquire GPU command buffer: {}", SDL_GetError()
			);
			return;
		}

		SDL_GPUTexture* swapchainTexture = nullptr;
		Uint32 swapchainWidth = 0;
		Uint32 swapchainHeight = 0;

		if (SDL_WaitAndAcquireGPUSwapchainTexture(
				commandBuffer,
				sdlWindow,
				&swapchainTexture,
				&swapchainWidth,
				&swapchainHeight
			) == false
		) {
			SOLAR_LOG_ERR(
				RENDER,
				"Failed to acquire swapchain texture: {}", SDL_GetError()
			);

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
		if (debugUiCallback != nullptr)
		{
			debugUiCallback();
		}
		imGuiLayer->PrepareDraw(commandBuffer);

		SDL_GPUColorTargetInfo target{};
		target.texture = swapchainTexture;
		target.clear_color = { 0.5f, 0.1f, 0.7f, 1.0f };
		target.load_op = SDL_GPU_LOADOP_CLEAR;
		target.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);

		// Aspect comes from the swapchain rather than the window, so it stays
		// correct through a resize without a separate resize notification.
		const float aspect = swapchainHeight > 0
			? static_cast<float>(swapchainWidth) / static_cast<float>(swapchainHeight)
			: 1.0f;

		const CameraData cameraData = MakeCameraData(in_camera, aspect);

		SDL_PushGPUFragmentUniformData(commandBuffer, 0, &cameraData, sizeof(cameraData));

		SDL_BindGPUGraphicsPipeline(renderPass, graphicsPipeline);
		SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);

		imGuiLayer->Draw(commandBuffer, renderPass);

		SDL_EndGPURenderPass(renderPass);
		SDL_SubmitGPUCommandBuffer(commandBuffer);
	}

	// TODO: This will need to change/be ripped out
	// 
	// The default should not be picked by this arbitrary order, and this isn't
	// even the place setting the present mode, so the output is a preemptive
	// claim which might not be true
	SDL_GPUPresentMode Renderer::PickPresentMode()
	{
		bool supportsVsync = SDL_WindowSupportsGPUPresentMode(gpuDevice, sdlWindow, SDL_GPU_PRESENTMODE_VSYNC);
		bool supportsImmediate = SDL_WindowSupportsGPUPresentMode(gpuDevice, sdlWindow, SDL_GPU_PRESENTMODE_IMMEDIATE);
		bool supportsMailbox = SDL_WindowSupportsGPUPresentMode(gpuDevice, sdlWindow, SDL_GPU_PRESENTMODE_MAILBOX);

		SOLAR_LOG_INFO(
			RENDER,
			"Supported GPU Present Modes:"
			"\n\t{}: {}"
			"\n\t{}: {}"
			"\n\t{}: {}",
			"Mailbox", supportsMailbox,
			"Immediate", supportsImmediate,
			"VSync", supportsVsync
		);

		SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;

		if (supportsMailbox)
		{
			presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
			SOLAR_LOG_INFO(RENDER, "Using Mailbox");
		}
		else if (supportsImmediate)
		{
			presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
			SOLAR_LOG_INFO(RENDER, "Using Immediate (might get screen tearing)");
		}
		else
		{
			presentMode = SDL_GPU_PRESENTMODE_VSYNC;
			SOLAR_LOG_INFO(RENDER, "Using Vsync");
		}

		return presentMode;
	}
}
