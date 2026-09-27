#include "solar/render/Renderer.hpp"

#include "solar/core/Log.hpp"
#include "solar/platform/Window.hpp"

#include <SDL3/SDL.h>

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
	}

	Renderer::~Renderer()
	{
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

		return Error::NONE;
	}

	void Renderer::Render()
	{
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
				nullptr) == false)
		{
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

		SDL_GPUColorTargetInfo target{};
		target.texture = swapchainTexture;
		target.clear_color = { 0.5f, 0.1f, 0.7f, 1.0f };
		target.load_op = SDL_GPU_LOADOP_CLEAR;
		target.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);
		SDL_EndGPURenderPass(renderPass);

		SDL_SubmitGPUCommandBuffer(commandBuffer);
	}
}
