#pragma once

#include "solar/core/DebugUiHook.hpp"
#include "solar/core/Error.hpp"
#include "solar/core/Math.hpp"
#include "solar/render/LightSettings.hpp"
#include "solar/render/ShadingSettings.hpp"
#include "solar/voxel/Palette.hpp"
#include "solar/voxel/VoxelGrid.hpp"

/* Forward declarations */
struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUTexture;
struct SDL_GPUSampler;
enum SDL_GPUPresentMode;

namespace solar
{
	class Window;
	class ImGuiLayer;
	struct Camera;

	class Renderer
	{
	public:
		Renderer() = default;
		~Renderer();

		// Owns a GPU device handle, so it is neither copyable nor movable.
		// Declaring the destructor already suppresses the implicit moves.
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		[[nodiscard]] Error Initialize(Window& in_window, DebugUiCallback in_debugUiCallback = nullptr);

		void Render(const Camera& in_camera, DebugUiContext& inout_debugUi);

		void SetShadingSettings(const ShadingSettings& in_settings) { shading = in_settings; }

		// Exposure travels with the rig because the two are tuned together, even though it is
		// applied in the present pass rather than with the lights
		void SetLightSettings(const LightSettings& in_settings) { lights = in_settings; }

		// Uploads a voxel model, replacing whatever was there. The renderer never loads files,
		// it just gets handed data that something else worked out how to fetch.
		[[nodiscard]] Error SetVoxelModel(const voxel::VoxelGrid& in_grid, const voxel::Palette& in_palette);
	private:
		SDL_Window* sdlWindow = nullptr;
		SDL_GPUDevice* gpuDevice = nullptr;
		SDL_GPUGraphicsPipeline* graphicsPipeline = nullptr;
		SDL_GPUGraphicsPipeline* presentPipeline = nullptr;

		// The scene renders here at a fixed internal height, then is upscaled by a
		// whole number of pixels so voxel edges stay crisp.
		SDL_GPUTexture* sceneTexture = nullptr;
		SDL_GPUSampler* nearestSampler = nullptr;
		ShadingSettings shading;
		LightSettings lights;
		int sceneWidth = 0;
		int sceneHeight = 0;
		SDL_GPUTexture* voxelTexture = nullptr;
		IVec3 voxelGridSize = { 0, 0, 0 };

		// Laid out exactly as the shader's float4[256], so it uploads directly.
		voxel::Palette voxelPalette;
		bool isClaimed = false;

		// Using a pointer so we can properly handle its lifetime
		ImGuiLayer* imGuiLayer = nullptr;
		DebugUiCallback debugUiCallback = nullptr;

		SDL_GPUPresentMode PickPresentMode();

		[[nodiscard]] bool EnsureSceneTexture(int in_width, int in_height);
	};
}
