#include "solar/render/Renderer.hpp"

#include "CameraData.hpp"
#include "ImGuiLayer.hpp"
#include "solar/core/Log.hpp"
#include "solar/platform/Window.hpp"
#include "solar/core/Filesystem.hpp"
#include "solar/voxel/VoxImport.hpp"

#include <SDL3/SDL.h>

#include <vector>

namespace solar
{
	namespace
	{
		// The internal height aimed for. The actual value is the divisor of the
		// window height nearest to this, so the upscale is a whole number of
		// pixels and still fills the screen. Fair view size is guaranteed by the
		// camera covering a fixed extent in world units, not by this being fixed.
		constexpr int preferredInternalHeight = 360;

		constexpr SDL_GPUTextureFormat sceneFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

		// Temporary, until the client owns models and hands them to the renderer.
		constexpr const char* defaultModelPath = "assets/models/test/density_40.vox";

		// Validation layers are expensive, so they are off in Shipping. NDEBUG is a
		// stand-in until the SE_ feature flags exist: Debug and Development strip it,
		// Shipping defines it.
#ifdef NDEBUG
		constexpr bool enableGpuDebug = false;
#else
		constexpr bool enableGpuDebug = true;
#endif

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
			0,
			0,
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
			2,
			1,
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

		if (CreateVoxelTexture(defaultModelPath) != Error::NONE)
		{
			return Error::RENDERER_FAILURE;
		}

		SOLAR_LOG_INFO(RENDER, "Initialized renderer");
		return Error::NONE;
	}

	Error Renderer::SetModel(const char* in_path)
	{
		// Release is deferred by SDL until the GPU has finished with the texture,
		// so the frame already in flight keeps reading the old one safely.
		if (voxelTexture != nullptr)
		{
			SDL_ReleaseGPUTexture(gpuDevice, voxelTexture);
			voxelTexture = nullptr;
		}

		return CreateVoxelTexture(in_path);
	}

	Error Renderer::CreateVoxelTexture(const char* in_path)
	{
		voxel::VoxelGrid grid;

		const Error importError = voxel::ImportVoxFile(in_path, grid, voxelPalette);

		if (importError != Error::NONE)
		{
			return importError;
		}

		SDL_GPUTextureCreateInfo textureInfo{};
		textureInfo.type = SDL_GPU_TEXTURETYPE_3D;
		textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8_UINT;
		textureInfo.usage = SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
		textureInfo.width = static_cast<Uint32>(grid.size.x);
		textureInfo.height = static_cast<Uint32>(grid.size.y);
		// Depth shares a field with array layer count for 2D arrays.
		textureInfo.layer_count_or_depth = static_cast<Uint32>(grid.size.z);
		textureInfo.num_levels = 1;

		voxelTexture = SDL_CreateGPUTexture(gpuDevice, &textureInfo);

		if (voxelTexture == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to create voxel texture: {}", SDL_GetError());
			return Error::RENDERER_FAILURE;
		}

		voxelGridSize = grid.size;

		SDL_GPUTransferBufferCreateInfo transferInfo{};
		transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transferInfo.size = static_cast<Uint32>(grid.materials.size());

		SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &transferInfo);

		if (transferBuffer == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to create transfer buffer: {}", SDL_GetError());
			return Error::RENDERER_FAILURE;
		}

		void* mapped = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false);

		if (mapped == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to map transfer buffer: {}", SDL_GetError());
			SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
			return Error::RENDERER_FAILURE;
		}

		SDL_memcpy(mapped, grid.materials.data(), grid.materials.size());
		SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

		SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);

		if (commandBuffer == nullptr)
		{
			SOLAR_LOG_ERR(RENDER, "Failed to acquire command buffer for upload: {}", SDL_GetError());
			SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
			return Error::RENDERER_FAILURE;
		}

		SDL_GPUTextureTransferInfo source{};
		source.transfer_buffer = transferBuffer;
		source.offset = 0;

		SDL_GPUTextureRegion destination{};
		destination.texture = voxelTexture;
		destination.w = static_cast<Uint32>(grid.size.x);
		destination.h = static_cast<Uint32>(grid.size.y);
		destination.d = static_cast<Uint32>(grid.size.z);

		SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
		SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
		SDL_EndGPUCopyPass(copyPass);

		SDL_SubmitGPUCommandBuffer(commandBuffer);

		// Safe to release immediately: SDL keeps it alive until the copy completes.
		SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);

		return Error::NONE;
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

	void Renderer::Render(const Camera& in_camera, DebugUiContext& inout_debugUi)
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

		const int scale = SDL_max(1, static_cast<int>(SDL_roundf(
			static_cast<float>(swapchainHeight) / preferredInternalHeight)));

		const int targetHeight = SDL_max(1, static_cast<int>(swapchainHeight) / scale);
		const int targetWidth = SDL_max(1, static_cast<int>(swapchainWidth) / scale);

		if (EnsureSceneTexture(targetWidth, targetHeight) == false)
		{
			SDL_CancelGPUCommandBuffer(commandBuffer);
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
			target.clear_color = { 0.05f, 0.05f, 0.08f, 1.0f };
			target.load_op = SDL_GPU_LOADOP_CLEAR;
			target.store_op = SDL_GPU_STOREOP_STORE;

			SDL_GPURenderPass* scenePass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);

			// Aspect from the scene target, not the window: rounding to whole
			// pixels means they differ slightly, and the rays must match what is
			// actually being rendered into.
			const float sceneAspect = static_cast<float>(sceneWidth) / static_cast<float>(sceneHeight);
			const CameraData cameraData = MakeCameraData(in_camera, sceneAspect, sceneHeight, voxelGridSize, shading);

			SDL_PushGPUFragmentUniformData(commandBuffer, 0, &cameraData, sizeof(cameraData));
			SDL_PushGPUFragmentUniformData(commandBuffer, 1, voxelPalette.colours.data(),
				static_cast<Uint32>(voxelPalette.colours.size() * sizeof(Vec4)));

			SDL_BindGPUGraphicsPipeline(scenePass, graphicsPipeline);
			SDL_BindGPUFragmentStorageTextures(scenePass, 0, &voxelTexture, 1);
			SDL_DrawGPUPrimitives(scenePass, 3, 1, 0, 0);

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

			SDL_GPUViewport viewport{};
			viewport.x = (static_cast<float>(swapchainWidth) - scaledWidth) * 0.5f;
			viewport.y = (static_cast<float>(swapchainHeight) - scaledHeight) * 0.5f;
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
				0.0f,
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
