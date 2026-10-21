#include "solar/render/Renderer.hpp"

#include "CameraData.hpp"
#include "EnvironmentData.hpp"
#include "ImGuiLayer.hpp"
#include "SceneData.hpp"
#include "solar/core/Log.hpp"
#include "solar/platform/Window.hpp"
#include "solar/core/Filesystem.hpp"
#include "solar/voxel/VoxelGrid.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <vector>

namespace solar
{
	namespace
	{
		// The internal height aimed for. The actual value is the divisor of the
		// window height nearest to this, so the upscale is a whole number of
		// pixels and still fills the screen. Fair view size is guaranteed by the
		// camera covering a fixed extent in world units, not by this being fixed.
		constexpr int preferredInternalHeight = 720;

		// Which whole-number upscale puts the internal height nearest the preferred one.
		//
		// Rounding the ratio instead - round(windowHeight / preferred) - is not the same thing and
		// leans the wrong way, because windowHeight / scale falls off non-linearly. A 974 line
		// window gives a ratio of 1.35, which rounds to 1 and renders all 974 lines natively, when
		// 487 is nearer to 720 and costs a quarter of the pixels. So each candidate is measured by
		// the height it actually produces
		int PickInternalScale(int in_windowHeight)
		{
			int scale = 1;
			int bestDistance = SDL_abs(in_windowHeight - preferredInternalHeight);

			// The height falls monotonically as the scale rises, so its distance from the preferred
			// height falls and then rises. The first candidate no better than the best so far is
			// past the turn, and a tie keeps the lower scale and the sharper image
			for (int candidate = 2; ; ++candidate)
			{
				const int height = in_windowHeight / candidate;

				if (height < 1)
				{
					break;
				}

				const int distance = SDL_abs(height - preferredInternalHeight);

				if (distance >= bestDistance)
				{
					break;
				}

				bestDistance = distance;
				scale = candidate;
			}

			return scale;
		}

		// Float rather than UNORM so lighting is not clamped at 1.0. Without the headroom the
		// three light terms have to sum under one, which forces a weak key and loses the warm
		// highlight exactly where it is strongest. Exposure, tonemap and the sRGB encode all
		// happen in the present pass instead.
		constexpr SDL_GPUTextureFormat sceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

		// Voxels per coarse cell. Bigger skips further per step but overshoots more into cells that
		// only just hold something; 8 is a reasonable middle and is the one number the shader and
		// this file have to agree on, so it travels as a uniform rather than being written twice
		constexpr int coarseCell = 16;

		// Validation layers are expensive, so they are off in Shipping. NDEBUG is a
		// stand-in until the SE_ feature flags exist: Debug and Development strip it,
		// Shipping defines it.
#ifdef NDEBUG
		constexpr bool enableGpuDebug = false;
#else
		constexpr bool enableGpuDebug = true;
#endif

		// TODO: This will need to change/be ripped out
		//
		// The default should not be picked by this arbitrary order, and this isn't
		// even the place setting the present mode, so the output is a preemptive
		// claim which might not be true
		SDL_GPUPresentMode PickPresentMode(SDL_GPUDevice* in_gpuDevice, SDL_Window* in_sdlWindow)
		{
			bool supportsVsync = SDL_WindowSupportsGPUPresentMode(in_gpuDevice, in_sdlWindow, SDL_GPU_PRESENTMODE_VSYNC);
			bool supportsImmediate = SDL_WindowSupportsGPUPresentMode(in_gpuDevice, in_sdlWindow, SDL_GPU_PRESENTMODE_IMMEDIATE);
			bool supportsMailbox = SDL_WindowSupportsGPUPresentMode(in_gpuDevice, in_sdlWindow, SDL_GPU_PRESENTMODE_MAILBOX);

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

		SDL_GPUTexture* CreateVoxelTexture(SDL_GPUDevice* in_gpuDevice, const IVec3& in_size)
		{
			SDL_GPUTextureCreateInfo info{};
			info.type = SDL_GPU_TEXTURETYPE_3D;
			info.format = SDL_GPU_TEXTUREFORMAT_R8_UINT;
			info.usage = SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
			info.width = static_cast<Uint32>(in_size.x);
			info.height = static_cast<Uint32>(in_size.y);
			// Depth shares a field with array layer count for 2D arrays
			info.layer_count_or_depth = static_cast<Uint32>(in_size.z);
			info.num_levels = 1;

			return SDL_CreateGPUTexture(in_gpuDevice, &info);
		}

		// Returns the transfer buffer rather than releasing it, because SDL only keeps it alive
		// until the copy completes and the caller owns the command buffer that will do it
		SDL_GPUTransferBuffer* UploadVoxels(
			SDL_GPUDevice* in_gpuDevice,
			SDL_GPUCopyPass* in_copyPass,
			SDL_GPUTexture* in_texture,
			const std::vector<uint8_t>& in_data,
			const IVec3& in_size,
			int in_offsetZ
		) {
			SDL_GPUTransferBufferCreateInfo transferInfo{};
			transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
			transferInfo.size = static_cast<Uint32>(in_data.size());

			SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(in_gpuDevice, &transferInfo);

			if (transferBuffer == nullptr)
			{
				SOLAR_LOG_ERR(RENDER, "Failed to create transfer buffer: {}", SDL_GetError());
				return nullptr;
			}

			void* mapped = SDL_MapGPUTransferBuffer(in_gpuDevice, transferBuffer, false);

			if (mapped == nullptr)
			{
				SOLAR_LOG_ERR(RENDER, "Failed to map transfer buffer: {}", SDL_GetError());
				SDL_ReleaseGPUTransferBuffer(in_gpuDevice, transferBuffer);
				return nullptr;
			}

			SDL_memcpy(mapped, in_data.data(), in_data.size());
			SDL_UnmapGPUTransferBuffer(in_gpuDevice, transferBuffer);

			SDL_GPUTextureTransferInfo source{};
			source.transfer_buffer = transferBuffer;

			SDL_GPUTextureRegion destination{};
			destination.texture = in_texture;
			destination.z = static_cast<Uint32>(in_offsetZ);
			destination.w = static_cast<Uint32>(in_size.x);
			destination.h = static_cast<Uint32>(in_size.y);
			destination.d = static_cast<Uint32>(in_size.z);

			SDL_UploadToGPUTexture(in_copyPass, &source, &destination, false);

			return transferBuffer;
		}

		SDL_GPUShader* LoadShader(SDL_GPUDevice* in_gpuDevice, const char* in_path, SDL_GPUShaderStage in_shaderStage, Uint32 in_numUniformBuffers, Uint32 in_numStorageTextures, Uint32 in_numSamplers)
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
			info.num_samplers = in_numSamplers;
			info.num_uniform_buffers = in_numUniformBuffers;
			info.num_storage_textures = in_numStorageTextures;

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

		if (nearestSampler != nullptr)
		{
			SDL_ReleaseGPUSampler(gpuDevice, nearestSampler);
		}

		if (sceneTexture != nullptr)
		{
			SDL_ReleaseGPUTexture(gpuDevice, sceneTexture);
		}

		if (presentPipeline != nullptr)
		{
			SDL_ReleaseGPUGraphicsPipeline(gpuDevice, presentPipeline);
		}

		if (coarseTexture != nullptr)
		{
			SDL_ReleaseGPUTexture(gpuDevice, coarseTexture);
		}

		if (voxelTexture != nullptr)
		{
			SDL_ReleaseGPUTexture(gpuDevice, voxelTexture);
		}

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

		// Checked up front rather than discovered as a null texture later. Every Vulkan
		// implementation we care about supports this, so a failure here means something is very
		// wrong and is worth saying so plainly
		if (SDL_GPUTextureSupportsFormat(
			gpuDevice,
			sceneFormat,
			SDL_GPU_TEXTURETYPE_2D,
			SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER
		) == false)
		{
			SOLAR_LOG_ERR(
				RENDER,
				"The GPU cannot use the HDR scene format as a colour target and sampler"
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

		SDL_GPUPresentMode presentMode = PickPresentMode(gpuDevice, sdlWindow);

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
			0,
			0,
			0
		);

		if (vertShader == nullptr)
		{
			return Error::RENDERER_FAILURE;
		}

		// Four uniform buffers: camera, palette, environment, scene. This has to match the number
		// of cbuffers the shader declares or the bindings silently slide along by one, and four
		// is also as many as SDL_GPU allows per stage
		SDL_GPUShader* fragShader = LoadShader(
			gpuDevice,
			"shaders/Fullscreen.frag.spv",
			SDL_GPU_SHADERSTAGE_FRAGMENT,
			4,
			2,
			0
		);

		if (fragShader == nullptr)
		{
			SDL_ReleaseGPUShader(gpuDevice, vertShader);
			return Error::RENDERER_FAILURE;
		}

		// The scene pipeline targets the offscreen texture, so its colour format is
		// that texture's, not the swapchain's.
		SDL_GPUColorTargetDescription sceneTarget{};
		sceneTarget.format = sceneFormat;

		SDL_GPUGraphicsPipelineCreateInfo info{};
		info.vertex_shader = vertShader;
		info.fragment_shader = fragShader;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

		info.target_info.color_target_descriptions = &sceneTarget;
		info.target_info.num_color_targets = 1;

		graphicsPipeline = SDL_CreateGPUGraphicsPipeline(gpuDevice, &info);

		SDL_ReleaseGPUShader(gpuDevice, fragShader);

		if (graphicsPipeline == nullptr)
		{
			SDL_ReleaseGPUShader(gpuDevice, vertShader);
			SOLAR_LOG_ERR(
				RENDER,
				"Failed to create graphics pipeline: {}", SDL_GetError()
			);
			return Error::RENDERER_FAILURE;
		}

		// The present pass reuses the same fullscreen triangle vertex shader.
		SDL_GPUShader* presentFragShader = LoadShader(
			gpuDevice,
			"shaders/Present.frag.spv",
			SDL_GPU_SHADERSTAGE_FRAGMENT,
			1,
			0,
			1
		);

		if (presentFragShader == nullptr)
		{
			SDL_ReleaseGPUShader(gpuDevice, vertShader);
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUColorTargetDescription presentTarget{};
		presentTarget.format = swapchainFormat;

		SDL_GPUGraphicsPipelineCreateInfo presentInfo{};
		presentInfo.vertex_shader = vertShader;
		presentInfo.fragment_shader = presentFragShader;
		presentInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

		presentInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		presentInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

		presentInfo.target_info.color_target_descriptions = &presentTarget;
		presentInfo.target_info.num_color_targets = 1;

		presentPipeline = SDL_CreateGPUGraphicsPipeline(gpuDevice, &presentInfo);

		SDL_ReleaseGPUShader(gpuDevice, vertShader);
		SDL_ReleaseGPUShader(gpuDevice, presentFragShader);

		if (presentPipeline == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to create present pipeline: {}", SDL_GetError());
			return Error::RENDERER_FAILURE;
		}

		// NEAREST is the whole point: linear filtering would blur the voxel edges
		// the low internal resolution exists to keep crisp.
		SDL_GPUSamplerCreateInfo samplerInfo{};
		samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
		samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
		samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

		nearestSampler = SDL_CreateGPUSampler(gpuDevice, &samplerInfo);

		if (nearestSampler == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to create sampler: {}", SDL_GetError());
			return Error::RENDERER_FAILURE;
		}

		SOLAR_LOG_INFO(RENDER, "Initialized renderer");
		return Error::NONE;
	}

	Error Renderer::SetVoxelModels(const VoxelModel* in_models, int in_count)
	{
		if (in_models == nullptr || in_count <= 0 || in_count > maxSceneModels)
		{
			SOLAR_LOG_ERR(RENDER, "A scene needs 1 to {} models, was given {}", maxSceneModels, in_count);
			return Error::INVALID_DATA;
		}

		// Models are stacked along Z, so the atlas is as wide and tall as the largest of them and as
		// deep as all of them together. Wasteful when their shapes disagree, and exactly what a real
		// brick pool replaces later
		IVec3 atlasSize = { 0, 0, 0 };
		IVec3 coarseAtlasSize = { 0, 0, 0 };

		// One palette serves the whole scene, so each colour a model actually uses is given its own
		// slot in it and the voxel data is remapped to match as it uploads. Counting only the used
		// colours matters because an artist can pick them from anywhere in a model's 256
		std::array<uint8_t, voxel::paletteSize> paletteRemaps[maxSceneModels]{};
		int paletteUsed = 0;

		// Everything is built into locals and only swapped in once nothing else can fail, so a bad
		// reload leaves the scene that is already running exactly as it was
		LoadedModel layout[maxSceneModels];

		for (int index = 0; index < in_count; ++index)
		{
			const VoxelModel& model = in_models[index];

			if (model.grid == nullptr || model.palette == nullptr || model.voxelSize <= 0.0f)
			{
				SOLAR_LOG_ERR(RENDER, "Model {} is missing a grid, a palette or a voxel size", index);
				return Error::INVALID_DATA;
			}

			const IVec3 size = model.grid->size;

			if (size.x <= 0 || size.y <= 0 || size.z <= 0)
			{
				SOLAR_LOG_ERR(RENDER, "Model {} has an empty axis", index);
				return Error::INVALID_DATA;
			}

			bool materialUsed[voxel::paletteSize]{};

			for (const uint8_t material : model.grid->materials)
			{
				materialUsed[material] = true;
			}

			// Slots past the end wrap, but the scene is rejected below before any of them is used
			for (size_t material = 1; material < voxel::paletteSize; ++material)
			{
				if (materialUsed[material])
				{
					paletteRemaps[index][material] = static_cast<uint8_t>(++paletteUsed);
				}
			}

			// Rounded up, so a model whose size is not a whole number of cells still gets a cell
			// covering the remainder
			const IVec3 coarseSize = (size + (coarseCell - 1)) / coarseCell;

			layout[index].atlasOffset = { 0, 0, atlasSize.z };
			layout[index].size = size;
			layout[index].voxelSize = model.voxelSize;
			layout[index].coarseOffset = { 0, 0, coarseAtlasSize.z };
			layout[index].coarseSize = coarseSize;

			atlasSize.x = SDL_max(atlasSize.x, size.x);
			atlasSize.y = SDL_max(atlasSize.y, size.y);
			atlasSize.z += size.z;

			coarseAtlasSize.x = SDL_max(coarseAtlasSize.x, coarseSize.x);
			coarseAtlasSize.y = SDL_max(coarseAtlasSize.y, coarseSize.y);
			coarseAtlasSize.z += coarseSize.z;
		}

		// Index 0 stays air, so only the rest are available to share out
		if (paletteUsed >= static_cast<int>(voxel::paletteSize))
		{
			SOLAR_LOG_ERR(
				RENDER,
				"Scene models need {} palette entries, only {} exist",
				paletteUsed, voxel::paletteSize - 1
			);
			return Error::INVALID_DATA;
		}

		// Created alongside the old atlas rather than in its place, so the old one is still there to
		// fall back on if anything from here fails. Memory briefly holds both
		SDL_GPUTexture* newVoxelTexture = CreateVoxelTexture(gpuDevice, atlasSize);
		SDL_GPUTexture* newCoarseTexture = CreateVoxelTexture(gpuDevice, coarseAtlasSize);

		const auto releaseNew = [this, newVoxelTexture, newCoarseTexture]()
		{
			if (newVoxelTexture != nullptr)
			{
				SDL_ReleaseGPUTexture(gpuDevice, newVoxelTexture);
			}

			if (newCoarseTexture != nullptr)
			{
				SDL_ReleaseGPUTexture(gpuDevice, newCoarseTexture);
			}
		};

		if (newVoxelTexture == nullptr || newCoarseTexture == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to create voxel atlas: {}", SDL_GetError());
			releaseNew();
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);

		if (commandBuffer == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to acquire command buffer for upload: {}", SDL_GetError());
			releaseNew();
			return Error::RENDERER_FAILURE;
		}

		voxel::Palette palette{};

		SDL_GPUTransferBuffer* transferBuffers[maxSceneModels]{};
		SDL_GPUTransferBuffer* coarseBuffers[maxSceneModels]{};
		SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);

		Error result = Error::NONE;

		for (int index = 0; index < in_count && result == Error::NONE; ++index)
		{
			const VoxelModel& model = in_models[index];
			const voxel::VoxelGrid& grid = *model.grid;
			const LoadedModel& loaded = layout[index];
			const std::array<uint8_t, voxel::paletteSize>& remap = paletteRemaps[index];

			for (size_t entry = 1; entry < voxel::paletteSize; ++entry)
			{
				if (remap[entry] != 0)
				{
					palette.colours[remap[entry]] = model.palette->colours[entry];
				}
			}

			// Both built in one pass: the materials remapped into the shared palette, and a cell
			// marked for every voxel that turns out to be solid. Air leaves each alone, so the
			// vectors start as all air and only the solid voxels are written
			std::vector<uint8_t> materials(grid.materials.size(), 0);
			std::vector<uint8_t> coarse(
				static_cast<size_t>(loaded.coarseSize.x) * loaded.coarseSize.y * loaded.coarseSize.z,
				0
			);

			for (int z = 0; z < grid.size.z; ++z)
			{
				for (int y = 0; y < grid.size.y; ++y)
				{
					for (int x = 0; x < grid.size.x; ++x)
					{
						const size_t at = voxel::VoxelIndex(grid.size, x, y, z);
						const uint8_t material = grid.materials[at];

						if (material == 0)
						{
							continue;
						}

						materials[at] = remap[material];

						coarse[voxel::VoxelIndex(
							loaded.coarseSize,
							x / coarseCell,
							y / coarseCell,
							z / coarseCell
						)] = 1;
					}
				}
			}

			transferBuffers[index] = UploadVoxels(
				gpuDevice, copyPass, newVoxelTexture, materials, grid.size, loaded.atlasOffset.z
			);

			coarseBuffers[index] = UploadVoxels(
				gpuDevice, copyPass, newCoarseTexture, coarse, loaded.coarseSize, loaded.coarseOffset.z
			);

			if (transferBuffers[index] == nullptr || coarseBuffers[index] == nullptr)
			{
				result = Error::RENDERER_FAILURE;
			}
		}
		SDL_EndGPUCopyPass(copyPass);
		SDL_SubmitGPUCommandBuffer(commandBuffer);

		// Safe to release immediately: SDL keeps each alive until its copy completes
		for (int index = 0; index < maxSceneModels; ++index)
		{
			if (transferBuffers[index] != nullptr)
			{
				SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffers[index]);
			}

			if (coarseBuffers[index] != nullptr)
			{
				SDL_ReleaseGPUTransferBuffer(gpuDevice, coarseBuffers[index]);
			}
		}

		if (result != Error::NONE)
		{
			releaseNew();
			return result;
		}

		// Release is deferred by SDL until the GPU has finished with the texture, so the frame
		// already in flight keeps reading the old one safely
		if (voxelTexture != nullptr)
		{
			SDL_ReleaseGPUTexture(gpuDevice, voxelTexture);
		}

		if (coarseTexture != nullptr)
		{
			SDL_ReleaseGPUTexture(gpuDevice, coarseTexture);
		}

		voxelTexture = newVoxelTexture;
		coarseTexture = newCoarseTexture;
		voxelPalette = palette;

		for (int index = 0; index < in_count; ++index)
		{
			loadedModels[index] = layout[index];
		}

		loadedModelCount = in_count;

		SOLAR_LOG_INFO(
			RENDER,
			"Uploaded {} models into a {}x{}x{} atlas with a {}x{}x{} skip volume, {} palette entries",
			in_count,
			atlasSize.x, atlasSize.y, atlasSize.z,
			coarseAtlasSize.x, coarseAtlasSize.y, coarseAtlasSize.z,
			paletteUsed
		);

		return Error::NONE;
	}

	SceneData Renderer::MakeSceneData(
		const VoxelInstance* in_instances,
		int in_instanceCount,
		const Vec3& in_cameraPosition
	) const {
		SceneData data;

		for (int index = 0; index < loadedModelCount; ++index)
		{
			const LoadedModel& model = loadedModels[index];

			data.modelOffset[index] = Vec4(model.atlasOffset, model.voxelSize);
			data.modelSize[index] = Vec4(model.size, 0.0f);
			data.modelCoarse[index] = Vec4(model.coarseOffset, static_cast<float>(coarseCell));
		}

		const int count = SDL_min(in_instanceCount, maxSceneInstances);

		// Each instance reduced to what the shader needs, plus how near it is to the camera
		struct Ordered
		{
			Vec3 position = { 0.0f, 0.0f, 0.0f };
			Vec3 sphereCentre = { 0.0f, 0.0f, 0.0f };
			float sphereRadius = 0.0f;
			float yaw = 0.0f;
			float scale = 1.0f;
			int model = 0;

			// To the nearest point on the sphere, so a large instance whose centre is far away
			// still sorts by the part of it the camera can actually reach first
			float distance = 0.0f;
		};

		Ordered ordered[maxSceneInstances];

		for (int index = 0; index < count; ++index)
		{
			const VoxelInstance& instance = in_instances[index];

			// Clamped rather than rejected, so a bad index draws the first model instead of
			// reading past the end of the array in the shader
			const int model = glm::clamp(instance.model, 0, SDL_max(loadedModelCount - 1, 0));

			// Floored rather than trusted, since a zero would collapse the model to nothing and
			// then be divided by. The sphere has to agree with it, so it is taken once
			const float scale = glm::max(instance.scale, 0.0001f);

			// The box is centred on x and z and rises from the base, so its middle sits directly
			// above the position - and a yaw turns that point onto itself, so the centre needs no
			// rotation. Nor does the radius, which is half the box diagonal either way
			const Vec3 extent = Vec3(loadedModels[model].size) * (loadedModels[model].voxelSize * scale);

			Ordered& entry = ordered[index];

			entry.position = instance.position;
			entry.sphereCentre = instance.position + Vec3(0.0f, extent.y * 0.5f, 0.0f);
			entry.sphereRadius = 0.5f * glm::length(extent);
			entry.yaw = instance.yaw;
			entry.scale = scale;
			entry.model = model;
			entry.distance = glm::length(entry.sphereCentre - in_cameraPosition) - entry.sphereRadius;
		}

		// Nearest first, so a camera ray finds its closest hit early and every instance behind it
		// is then turned away by comparison instead of being traversed. The sphere test cannot do
		// that on its own, because until something has been hit there is no distance to beat.
		// Shadow rays travel a different way and gain nothing from this order, but lose nothing
		// to it either
		std::sort(ordered, ordered + count, [](const Ordered& in_a, const Ordered& in_b)
		{
			return in_a.distance < in_b.distance;
		});

		for (int index = 0; index < count; ++index)
		{
			const Ordered& entry = ordered[index];

			data.instanceTransform[index] = Vec4(entry.position, static_cast<float>(entry.model));

			// Taken once here rather than per pixel, which is the only reason the yaw is not
			// simply handed over as an angle
			data.instanceRotation[index] = Vec4(
				glm::sin(entry.yaw),
				glm::cos(entry.yaw),
				entry.scale,
				0.0f
			);

			data.instanceSphere[index] = Vec4(entry.sphereCentre, entry.sphereRadius);
		}

		data.counts.x = static_cast<float>(count);

		return data;
	}

	bool Renderer::EnsureSceneTexture(int in_width, int in_height)
	{
		if (sceneTexture != nullptr && sceneWidth == in_width && sceneHeight == in_height)
		{
			return true;
		}

		if (sceneTexture != nullptr)
		{
			SDL_ReleaseGPUTexture(gpuDevice, sceneTexture);
			sceneTexture = nullptr;
		}

		SDL_GPUTextureCreateInfo info{};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = sceneFormat;
		info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = static_cast<Uint32>(in_width);
		info.height = static_cast<Uint32>(in_height);
		info.layer_count_or_depth = 1;
		info.num_levels = 1;

		sceneTexture = SDL_CreateGPUTexture(gpuDevice, &info);

		if (sceneTexture == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to create scene texture: {}", SDL_GetError());
			sceneWidth = 0;
			sceneHeight = 0;
			return false;
		}

		sceneWidth = in_width;
		sceneHeight = in_height;

		SOLAR_LOG_INFO(RENDER, "Scene target is {}x{}", sceneWidth, sceneHeight);

		return true;
	}

	void Renderer::Render(
		const Camera& in_camera,
		const VoxelInstance* in_instances,
		int in_instanceCount,
		DebugUiContext& inout_debugUi
	) {
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

		const int scale = PickInternalScale(static_cast<int>(swapchainHeight));

		const int targetHeight = SDL_max(1, static_cast<int>(swapchainHeight) / scale);
		const int targetWidth = SDL_max(1, static_cast<int>(swapchainWidth) / scale);

		if (EnsureSceneTexture(targetWidth, targetHeight) == false)
		{
			// Submitted rather than cancelled: SDL makes it an error to cancel a command buffer
			// that has acquired a swapchain texture, so the only way out from here is to send an
			// empty frame
			SDL_SubmitGPUCommandBuffer(commandBuffer);
			return;
		}

		imGuiLayer->NewFrame();

		if (debugUiCallback != nullptr)
		{
			// Filled in here rather than by the caller: the internal resolution is
			// derived from the swapchain, so this is the only place that knows it.
			inout_debugUi.sceneWidth = sceneWidth;
			inout_debugUi.sceneHeight = sceneHeight;

			debugUiCallback(inout_debugUi);
		}

		// Uploads buffers through a copy pass, so it cannot run inside a render pass.
		imGuiLayer->PrepareDraw(commandBuffer);

		// Pass 1: the scene, at internal resolution.
		{
			SDL_GPUColorTargetInfo target{};
			target.texture = sceneTexture;

			// The scene shader is a fullscreen triangle that returns a colour on every path, so
			// every texel is written and there is nothing here worth clearing or preserving.
			// If a pass is ever added that does not cover the target, this goes back to a clear -
			// the symptom would be the previous frame showing through rather than a known colour
			target.load_op = SDL_GPU_LOADOP_DONT_CARE;
			target.store_op = SDL_GPU_STOREOP_STORE;

			// Until a scene has uploaded there is no atlas to bind, so the target is cleared and
			// nothing is drawn rather than handing the shader a null texture
			const bool hasScene = voxelTexture != nullptr && coarseTexture != nullptr;

			if (hasScene == false)
			{
				target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
				target.load_op = SDL_GPU_LOADOP_CLEAR;
			}

			SDL_GPURenderPass* scenePass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);

			// Aspect from the scene target, not the window: rounding to whole
			// pixels means they differ slightly, and the rays must match what is
			// actually being rendered into.
			const float sceneAspect = static_cast<float>(sceneWidth) / static_cast<float>(sceneHeight);
			const CameraData cameraData = MakeCameraData(in_camera, sceneAspect, sceneHeight, shading);

			const EnvironmentData environmentData = MakeEnvironmentData(lights, atmosphere);
			const SceneData sceneData = MakeSceneData(in_instances, in_instanceCount, in_camera.position);

			SDL_PushGPUFragmentUniformData(commandBuffer, 0, &cameraData, sizeof(cameraData));
			SDL_PushGPUFragmentUniformData(commandBuffer, 1, voxelPalette.colours.data(),
				static_cast<Uint32>(voxelPalette.colours.size() * sizeof(Vec4)));
			SDL_PushGPUFragmentUniformData(commandBuffer, 2, &environmentData, sizeof(environmentData));
			SDL_PushGPUFragmentUniformData(commandBuffer, 3, &sceneData, sizeof(sceneData));

			if (hasScene)
			{
				SDL_BindGPUGraphicsPipeline(scenePass, graphicsPipeline);
				SDL_GPUTexture* const volumes[] = { voxelTexture, coarseTexture };

				SDL_BindGPUFragmentStorageTextures(scenePass, 0, volumes, 2);
				SDL_DrawGPUPrimitives(scenePass, 3, 1, 0, 0);
			}

			SDL_EndGPURenderPass(scenePass);
		}

		// Pass 2: upscale to the swapchain by a whole number of pixels, then the UI
		// on top at full resolution.
		{
			SDL_GPUColorTargetInfo target{};
			target.texture = swapchainTexture;
			target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
			target.load_op = SDL_GPU_LOADOP_CLEAR;
			target.store_op = SDL_GPU_STOREOP_STORE;

			SDL_GPURenderPass* presentPass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);

			const int scaledWidth = sceneWidth * scale;
			const int scaledHeight = sceneHeight * scale;

			// Floored to whole pixels. A viewport starting on a half pixel offsets the whole
			// nearest-neighbour fetch by half a texel, which makes some upscaled voxel blocks a
			// pixel wider than their neighbours - the exact artefact the integer scale avoids
			SDL_GPUViewport viewport{};
			viewport.x = static_cast<float>((static_cast<int>(swapchainWidth) - scaledWidth) / 2);
			viewport.y = static_cast<float>((static_cast<int>(swapchainHeight) - scaledHeight) / 2);
			viewport.w = static_cast<float>(scaledWidth);
			viewport.h = static_cast<float>(scaledHeight);
			viewport.min_depth = 0.0f;
			viewport.max_depth = 1.0f;

			SDL_SetGPUViewport(presentPass, &viewport);

			SDL_GPUTextureSamplerBinding binding{};
			binding.texture = sceneTexture;
			binding.sampler = nearestSampler;

			const Vec4 presentData = {
				static_cast<float>(sceneWidth),
				static_cast<float>(sceneHeight),
				lights.exposure,
				0.0f
			};

			SDL_PushGPUFragmentUniformData(commandBuffer, 0, &presentData, sizeof(presentData));

			SDL_BindGPUGraphicsPipeline(presentPass, presentPipeline);
			SDL_BindGPUFragmentSamplers(presentPass, 0, &binding, 1);
			SDL_DrawGPUPrimitives(presentPass, 3, 1, 0, 0);

			// The UI is not part of the upscale, so it draws over the whole window.
			SDL_GPUViewport fullViewport{};
			fullViewport.w = static_cast<float>(swapchainWidth);
			fullViewport.h = static_cast<float>(swapchainHeight);
			fullViewport.min_depth = 0.0f;
			fullViewport.max_depth = 1.0f;

			SDL_SetGPUViewport(presentPass, &fullViewport);

			imGuiLayer->Draw(commandBuffer, presentPass);

			SDL_EndGPURenderPass(presentPass);
		}

		SDL_SubmitGPUCommandBuffer(commandBuffer);
	}
}
