#pragma once

#include "solar/core/DebugUiHook.hpp"
#include "solar/core/Error.hpp"
#include "solar/core/Math.hpp"
#include "solar/render/AtmosphereSettings.hpp"
#include "solar/render/LightSettings.hpp"
#include "solar/render/ShadingSettings.hpp"
#include "solar/render/VoxelModel.hpp"
#include "solar/voxel/Palette.hpp"

#include <memory>

namespace solar
{
	struct SceneData;
	struct GpuResources;
	class Window;
	struct Camera;

	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		// Owns the GPU device and ImGui's global context, so it is neither copyable nor movable.
		// Declaring the destructor already suppresses the implicit moves
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
		// Private to the renderer so no SDL type reaches this header. Null until Initialize
		std::unique_ptr<GpuResources> gpu;

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

			IVec3 coarseOffset = { 0, 0, 0 };
			IVec3 coarseSize = { 0, 0, 0 };
		};

		LoadedModel loadedModels[maxSceneModels];
		int loadedModelCount = 0;

		// Laid out exactly as the shader's float4[256], so it uploads directly.
		voxel::Palette voxelPalette;

		DebugUiCallback debugUiCallback = nullptr;

		// SceneData is private to the renderer, which a declaration may name without defining
		SceneData MakeSceneData(
			const VoxelInstance* in_instances,
			int in_instanceCount,
			const Vec3& in_cameraPosition
		) const;

		[[nodiscard]] bool EnsureSceneTexture(int in_width, int in_height);
	};
}
