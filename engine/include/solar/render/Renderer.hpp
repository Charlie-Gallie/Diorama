#pragma once

#include "solar/core/DebugUiHook.hpp"
#include "solar/core/Error.hpp"
#include "solar/core/Math.hpp"
#include "solar/render/AtmosphereSettings.hpp"
#include "solar/render/LightSettings.hpp"
#include "solar/render/ShadingSettings.hpp"
#include "solar/render/VoxelModel.hpp"
#include "solar/voxel/Palette.hpp"

/* Forward declarations */
struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUTexture;
struct SDL_GPUSampler;
enum SDL_GPUPresentMode;

namespace solar
{
	struct SceneData;
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

		// The client decides what the scene contains and submits it every frame. The renderer
		// only draws what it is handed
		void Render(
			const Camera& in_camera,
			const VoxelInstance* in_instances,
			int in_instanceCount,
			DebugUiContext& inout_debugUi
		);

		void SetShadingSettings(const ShadingSettings& in_settings) { shading = in_settings; }

		// Exposure travels with the rig because the two are tuned together, even though it is
		// applied in the present pass rather than with the lights
		void SetLightSettings(const LightSettings& in_settings) { lights = in_settings; }

		void SetAtmosphereSettings(const AtmosphereSettings& in_settings) { atmosphere = in_settings; }

		// Uploads the set of models a scene may instance, replacing whatever was there. The
		// renderer never loads files, it just gets handed data that something else worked out
		// how to fetch
		[[nodiscard]] Error SetVoxelModels(const VoxelModel* in_models, int in_count);
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
		AtmosphereSettings atmosphere;
		int sceneWidth = 0;
		int sceneHeight = 0;
		// Where one model sits inside the shared atlas, and how big its voxels are. The only
		// place world scale lives on this side
		struct LoadedModel
		{
			IVec3 atlasOffset = { 0, 0, 0 };
			IVec3 size = { 0, 0, 0 };
			float voxelSize = 0.0f;
		};

		// Every model shares one 3D texture, so a scene draws from a single binding
		SDL_GPUTexture* voxelTexture = nullptr;
		LoadedModel loadedModels[maxSceneModels];
		int loadedModelCount = 0;

		// Laid out exactly as the shader's float4[256], so it uploads directly.
		voxel::Palette voxelPalette;
		bool isClaimed = false;

		// Using a pointer so we can properly handle its lifetime
		ImGuiLayer* imGuiLayer = nullptr;
		DebugUiCallback debugUiCallback = nullptr;

		// SceneData is private to the renderer, which a declaration may name without defining
		SceneData MakeSceneData(const VoxelInstance* in_instances, int in_instanceCount) const;

		SDL_GPUPresentMode PickPresentMode();

		[[nodiscard]] bool EnsureSceneTexture(int in_width, int in_height);
	};
}
