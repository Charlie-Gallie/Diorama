#include "solar/devtools/DevTools.hpp"

#include "CameraPanel.hpp"
#include "Performance.hpp"

namespace solar::devtools
{
	void DrawAll(DebugUiContext& inout_context)
	{
		DrawPerformancePanel();
		DrawCameraPanel(inout_context);
	}
}
