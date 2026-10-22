#pragma once

#include "ImGuiLayer.hpp"
#include "SceneShared.hlsli"
#include "solar/core/Math.hpp"

#include <SDL3/SDL.h>

#include <vector>

namespace solar
{
	inline void ReleaseGpuObject(SDL_GPUDevice* in_device, SDL_GPUTexture* in_texture)
	{
		SDL_ReleaseGPUTexture(in_device, in_texture);
	}
	inline void ReleaseGpuObject(SDL_GPUDevice* in_device, SDL_GPUSampler* in_sampler)
	{
		SDL_ReleaseGPUSampler(in_device, in_sampler);
	}
	inline void ReleaseGpuObject(SDL_GPUDevice* in_device, SDL_GPUShader* in_shader)
	{
		SDL_ReleaseGPUShader(in_device, in_shader);
	}
	inline void ReleaseGpuObject(SDL_GPUDevice* in_device, SDL_GPUGraphicsPipeline* in_pipeline)
	{
		SDL_ReleaseGPUGraphicsPipeline(in_device, in_pipeline);
	}
	inline void ReleaseGpuObject(SDL_GPUDevice* in_device, SDL_GPUTransferBuffer* in_buffer)
	{
		SDL_ReleaseGPUTransferBuffer(in_device, in_buffer);
	}
	inline void ReleaseGpuObject(SDL_GPUDevice* in_device, SDL_GPUBuffer* in_buffer)
	{
		SDL_ReleaseGPUBuffer(in_device, in_buffer);
	}

	// Owns one SDL_GPU object and releases it through the device that made it, so no path out of
	// a function has to remember to
	template <typename T>
	class GpuHandle
	{
	public:
		GpuHandle() = default;

		GpuHandle(SDL_GPUDevice* in_device, T* in_handle) :
			device(in_device),
			handle(in_handle)
		{
		}

		~GpuHandle() { Reset(); }

		GpuHandle(const GpuHandle&) = delete;
		GpuHandle& operator=(const GpuHandle&) = delete;

		GpuHandle(GpuHandle&& in_other) noexcept :
			device(in_other.device),
			handle(in_other.handle)
		{
			in_other.handle = nullptr;
		}

		GpuHandle& operator=(GpuHandle&& in_other) noexcept
		{
			if (this != &in_other)
			{
				Reset();

				device = in_other.device;
				handle = in_other.handle;

				in_other.handle = nullptr;
			}

			return *this;
		}

		// SDL defers the release until the GPU has finished with the object, so this is safe while
		// a frame that uses it is still in flight
		void Reset()
		{
			if (handle != nullptr)
			{
				ReleaseGpuObject(device, handle);
				handle = nullptr;
			}
		}

		T* Get() const { return handle; }

	private:
		SDL_GPUDevice* device = nullptr;
		T* handle = nullptr;
	};

	// Owns the device and, once claimed, the window's swapchain, which has to be given back before
	// the device goes
	class GpuDevice
	{
	public:
		GpuDevice() = default;
		~GpuDevice() { Reset(); }

		GpuDevice(const GpuDevice&) = delete;
		GpuDevice& operator=(const GpuDevice&) = delete;

		GpuDevice(GpuDevice&& in_other) noexcept :
			device(in_other.device),
			claimedWindow(in_other.claimedWindow)
		{
			in_other.device = nullptr;
			in_other.claimedWindow = nullptr;
		}

		GpuDevice& operator=(GpuDevice&& in_other) noexcept
		{
			if (this != &in_other)
			{
				Reset();

				device = in_other.device;
				claimedWindow = in_other.claimedWindow;

				in_other.device = nullptr;
				in_other.claimedWindow = nullptr;
			}

			return *this;
		}

		[[nodiscard]] bool Create(SDL_GPUShaderFormat in_formats, bool in_debug)
		{
			Reset();
			device = SDL_CreateGPUDevice(in_formats, in_debug, nullptr);
			return device != nullptr;
		}

		[[nodiscard]] bool ClaimWindow(SDL_Window* in_window)
		{
			if (SDL_ClaimWindowForGPUDevice(device, in_window) == false)
			{
				return false;
			}

			claimedWindow = in_window;
			return true;
		}

		void Reset()
		{
			if (claimedWindow != nullptr)
			{
				SDL_ReleaseWindowFromGPUDevice(device, claimedWindow);
				claimedWindow = nullptr;
			}

			if (device != nullptr)
			{
				SDL_DestroyGPUDevice(device);
				device = nullptr;
			}
		}

		SDL_GPUDevice* Get() const { return device; }
		SDL_Window* GetClaimedWindow() const { return claimedWindow; }

	private:
		SDL_GPUDevice* device = nullptr;
		SDL_Window* claimedWindow = nullptr;
	};

	// Where one model sits inside the shared atlas, and how big its voxels are. The only place world
	// scale lives on this side
	struct LoadedModel
	{
		IVec3 atlasOffset = { 0, 0, 0 };
		IVec3 size = { 0, 0, 0 };
		float voxelSize = 0.0f;

		IVec3 coarseOffset = { 0, 0, 0 };
		IVec3 coarseSize = { 0, 0, 0 };
	};

	// An instance as the shader wants it, plus how near it is to the camera for the sort
	struct SortedInstance
	{
		shader::InstanceData data;

		// To the nearest point on the bounding sphere, so a large instance whose centre is far away
		// still sorts by the part of it the camera can actually reach first
		float distance = 0.0f;
	};

	// Everything the renderer holds on the GPU. Members are destroyed in reverse order, so the
	// device is declared first to outlive everything made from it, and ImGui last so it shuts
	// down while the device it draws with still exists
	struct GpuResources
	{
		GpuDevice device;

		GpuHandle<SDL_GPUGraphicsPipeline> graphicsPipeline;
		GpuHandle<SDL_GPUGraphicsPipeline> presentPipeline;

		// The scene renders here at a fixed internal height, then is upscaled by a whole number
		// of pixels so voxel edges stay crisp
		GpuHandle<SDL_GPUTexture> sceneTexture;
		GpuHandle<SDL_GPUSampler> nearestSampler;

		// Every model shares one 3D texture, so a scene draws from a single binding. The coarse
		// texture is the same thing one cell per block of voxels, for skipping empty space
		GpuHandle<SDL_GPUTexture> voxelTexture;
		GpuHandle<SDL_GPUTexture> coarseTexture;

		// Describes the atlas, so it is rebuilt with it and a reload swaps both together
		GpuHandle<SDL_GPUBuffer> modelBuffer;
		std::vector<LoadedModel> loadedModels;

		// Refilled every frame. Grown when a frame has more instances than fit and never shrunk.
		// The transfer buffer is cycled as it is mapped, so a frame still in flight keeps the copy
		// it was given
		GpuHandle<SDL_GPUBuffer> instanceBuffer;
		GpuHandle<SDL_GPUTransferBuffer> instanceTransfer;
		int instanceCapacity = 0;

		// Kept between frames so that sorting the instances does not allocate every frame
		std::vector<SortedInstance> sortedInstances;

		ImGuiLayer imGuiLayer;
	};
}
