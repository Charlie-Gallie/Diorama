#include "CameraPanel.hpp"

#include "solar/core/DebugUiHook.hpp"
#include "solar/core/Log.hpp"
#include "solar/render/Camera.hpp"
#include "solar/render/ShadingSettings.hpp"

#include <imgui.h>

#include <cstdio>

namespace solar::devtools
{
	namespace
	{
		constexpr float panelWidth = 340.0f;
		constexpr float buttonWidth = 150.0f;

		// Only used to report how many voxels land in a pixel at the current
		// framing, so it is a readout and not tied to which model is loaded.
		constexpr int reportedDensities[] = { 50, 40, 30, 20, 10 };

		void DrawPresets(DebugUiContext& inout_context, Camera& inout_camera)
		{
			for (int index = 0; index < inout_context.cameraPresetCount; ++index)
			{
				const CameraPreset& preset = inout_context.cameraPresets[index];

				if (index % 2 != 0)
				{
					ImGui::SameLine();
				}

				if (ImGui::Button(preset.name, ImVec2(buttonWidth, 0.0f)))
				{
					inout_camera = preset.camera;
				}

				if (preset.hint != nullptr && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", preset.hint);
				}
			}
		}

		void DrawTransform(Camera& inout_camera, float in_metresPerUnit)
		{
			// Edited in degrees because that is how the values are talked about, and
			// converted back only when they actually change.
			float yawDegrees = glm::degrees(inout_camera.yaw);
			float pitchDegrees = glm::degrees(inout_camera.pitch);
			float fovYDegrees = glm::degrees(inout_camera.fovY);

			if (ImGui::DragFloat("Yaw", &yawDegrees, 0.5f, -360.0f, 360.0f, "%.1f deg"))
			{
				inout_camera.yaw = glm::radians(yawDegrees);
			}

			if (ImGui::DragFloat("Pitch", &pitchDegrees, 0.5f, -89.9f, 89.9f, "%.1f deg"))
			{
				inout_camera.pitch = glm::radians(pitchDegrees);
			}

			if (ImGui::DragFloat("Field of view", &fovYDegrees, 0.2f, 1.0f, 120.0f, "%.1f deg"))
			{
				inout_camera.fovY = glm::radians(fovYDegrees);
			}

			Vec3 positionMetres = inout_camera.position * in_metresPerUnit;

			if (ImGui::DragFloat3("Position", &positionMetres.x, 0.05f, 0.0f, 0.0f, "%.2f m"))
			{
				inout_camera.position = positionMetres / in_metresPerUnit;
			}
		}

		void DrawFraming(const DebugUiContext& in_context, const Camera& in_camera)
		{
			// Distance to the origin, which is where the presets aim and roughly the
			// middle of the test scene, so the numbers describe what is being looked at.
			const float focusDistance = glm::length(in_camera.position) * in_context.metresPerUnit;
			const float viewMetres = 2.0f * focusDistance * glm::tan(in_camera.fovY * 0.5f);

			ImGui::Text("Focus distance   %6.2f m", focusDistance);
			ImGui::Text("Vertical view    %6.2f m", viewMetres);
			ImGui::Text("Internal target  %d x %d", in_context.sceneWidth, in_context.sceneHeight);

			if (in_context.sceneHeight <= 0 || viewMetres <= 0.0f)
			{
				return;
			}

			const float pixelsPerMetre = in_context.sceneHeight / viewMetres;

			ImGui::Text("Pixels per metre %6.1f", pixelsPerMetre);
			ImGui::Spacing();

			// Above 1.0 the voxels are smaller than a pixel: detail that cannot be
			// seen but still costs memory. Below 1.0 the voxels are visible blocks.
			ImGui::TextUnformatted("Voxels per pixel at this framing");

			for (const int density : reportedDensities)
			{
				const float voxelsPerPixel = density / pixelsPerMetre;

				ImGui::Text("  %2d vox/m   %5.2f  %s",
					density,
					voxelsPerPixel,
					voxelsPerPixel > 1.05f ? "(finer than the screen)"
						: voxelsPerPixel < 0.95f ? "(visible blocks)"
						: "(1:1)");
			}
		}

		void DrawShading(ShadingSettings& inout_shading)
		{
			ImGui::Checkbox("Smooth normals  [1]", &inout_shading.smoothNormals);

			// Widening the radius is what removes the speckle a close-up shows: the
			// staircase is what the gradient picks up when it is measured too tightly.
			ImGui::BeginDisabled(inout_shading.smoothNormals == false);
			ImGui::DragFloat("Normal radius", &inout_shading.normalRadius, 0.05f, 0.5f, 8.0f, "%.2f voxels");
			ImGui::DragFloat("Smooth above", &inout_shading.normalFadeVoxels, 0.02f, 0.05f, 4.0f, "%.2f vox/px");
			ImGui::DragFloat("Concavity guard", &inout_shading.concavityGuard, 0.02f, 0.0f, 2.0f, "%.2f");
			ImGui::EndDisabled();

			ImGui::DragFloat("Axis tint", &inout_shading.axisTint, 0.01f, 0.0f, 1.0f, "%.2f");
		}

		void DrawModel(DebugUiContext& inout_context)
		{
			for (int index = 0; index < inout_context.modelCount; ++index)
			{
				if (index != 0)
				{
					ImGui::SameLine();
				}

				ImGui::RadioButton(inout_context.modelNames[index], inout_context.modelIndex, index);
			}
		}

		void DrawDensity(DebugUiContext& inout_context)
		{
			for (int index = 0; index < inout_context.densityCount; ++index)
			{
				if (index != 0)
				{
					ImGui::SameLine();
				}

				ImGui::RadioButton(inout_context.densityNames[index], inout_context.densityIndex, index);
			}
		}

		// One block of text covering everything that would be needed to reproduce
		// the current view, so a setting worth keeping can be copied out or logged.
		void DrawSettingsText(const DebugUiContext& in_context, const Camera& in_camera)
		{
			const float metresPerUnit = in_context.metresPerUnit;
			const Vec3 positionMetres = in_camera.position * metresPerUnit;
			const float focusDistance = glm::length(in_camera.position) * metresPerUnit;
			const float viewMetres = 2.0f * focusDistance * glm::tan(in_camera.fovY * 0.5f);

			const ShadingSettings shading = in_context.shading != nullptr
				? *in_context.shading
				: ShadingSettings{};
			const int density = in_context.densityIndex != nullptr ? *in_context.densityIndex : 0;

			const char* densityName = "unknown";

			if (in_context.densityNames != nullptr && density < in_context.densityCount)
			{
				densityName = in_context.densityNames[density];
			}

			char text[512];

			std::snprintf(text, sizeof(text),
				"position   %.3f, %.3f, %.3f  units\n"
				"           %.2f, %.2f, %.2f  metres\n"
				"yaw %.1f deg   pitch %.1f deg   fovY %.1f deg\n"
				"vertical view %.2f m at %.2f m   %s vox/m\n"
				"%s   radius %.2f   smooth above %.2f   axis tint %.2f\n"
				"MakeOrbitCamera(target, %.1ff, %.1ff, %.3ff, %.1ff)",
				in_camera.position.x, in_camera.position.y, in_camera.position.z,
				positionMetres.x, positionMetres.y, positionMetres.z,
				glm::degrees(in_camera.yaw), glm::degrees(in_camera.pitch), glm::degrees(in_camera.fovY),
				viewMetres, focusDistance, densityName,
				shading.smoothNormals ? "smooth normals" : "flat normals",
				shading.normalRadius, shading.normalFadeVoxels, shading.axisTint,
				glm::degrees(in_camera.yaw), glm::degrees(in_camera.pitch),
				glm::length(in_camera.position), glm::degrees(in_camera.fovY));

			ImGui::TextUnformatted(text);

			if (ImGui::Button("Copy", ImVec2(buttonWidth, 0.0f)))
			{
				ImGui::SetClipboardText(text);
			}

			ImGui::SameLine();

			// Logging puts it in the same place as everything else worth keeping
			// from a run, rather than only on the clipboard.
			if (ImGui::Button("Log", ImVec2(buttonWidth, 0.0f)))
			{
				SOLAR_LOG_INFO(RENDER, "Camera settings\n{}", text);
			}
		}
	}

	void DrawCameraPanel(DebugUiContext& inout_context)
	{
		if (inout_context.camera == nullptr)
		{
			return;
		}

		Camera& camera = *inout_context.camera;

		// Down the right-hand edge, out of the performance panel's way, and a fixed
		// size rather than auto-resizing so a tall panel scrolls instead of running
		// off the bottom of the window.
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(
			ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - panelWidth - 10.0f, viewport->WorkPos.y + 10.0f),
			ImGuiCond_FirstUseEver
		);

		ImGui::SetNextWindowSize(ImVec2(panelWidth, viewport->WorkSize.y - 20.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin("Camera");

		ImGui::SeparatorText("Presets");
		DrawPresets(inout_context, camera);
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("Keys 5 and 6 pick the first two. Right mouse looks, WASDQE moves, shift to hurry.");
		ImGui::PopStyleColor();

		ImGui::SeparatorText("Transform");
		DrawTransform(camera, inout_context.metresPerUnit);

		ImGui::SeparatorText("Framing");
		DrawFraming(inout_context, camera);

		if (inout_context.shading != nullptr)
		{
			ImGui::SeparatorText("Shading");
			DrawShading(*inout_context.shading);
		}

		if (inout_context.modelIndex != nullptr && inout_context.modelNames != nullptr)
		{
			ImGui::SeparatorText("Model");
			DrawModel(inout_context);
		}

		if (inout_context.densityIndex != nullptr && inout_context.densityNames != nullptr)
		{
			ImGui::SeparatorText("Voxel density");
			DrawDensity(inout_context);
		}

		ImGui::SeparatorText("Current settings");
		DrawSettingsText(inout_context, camera);

		ImGui::End();
	}
}
