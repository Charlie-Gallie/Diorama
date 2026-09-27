#include "Performance.hpp"

#include <imgui.h>

namespace solar::devtools
{
	constexpr int PLOT_SAMPLES = 1000;

	void DrawPerformancePanel()
	{
		static int sampleIndex = 0;
		static float frameTimeSamples[PLOT_SAMPLES] = {};

		ImGui::SetNextWindowBgAlpha(0.2f);
		ImGui::SetNextWindowPos(ImVec2(10, 10));
		ImGui::Begin(
			"Performance",
			nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav
		);

		ImGuiIO& io = ImGui::GetIO();
		float frameTimeMs = io.DeltaTime * 1000;
		float fps = io.Framerate;

		ImGui::Text("FPS: %.2f", fps);
		ImGui::Text("Frame Time (ms): %.2f", frameTimeMs);

		if (sampleIndex == PLOT_SAMPLES)
		{
			sampleIndex = 0;
		}
		frameTimeSamples[sampleIndex] = frameTimeMs;
		++sampleIndex;

		ImGui::PlotLines(
			"##devtools_performance_frametime",
			frameTimeSamples,
			PLOT_SAMPLES,
			sampleIndex,
			nullptr,
			0.0f, // Scale min
			33.3f, // Scale max TODO: Assumes vsync
			ImVec2(-1.0f, 80.0f)
		);

		ImGui::End();
	}
}