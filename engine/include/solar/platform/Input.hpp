#pragma once

#include "solar/core/Math.hpp"

namespace solar::input
{
	enum class Key
	{
		W,
		A,
		S,
		D,
		Q,
		E,
		LEFT_SHIFT,
		NUM_1,
		NUM_2,
		NUM_3,
		NUM_4,
		NUM_5,
		NUM_6,
		NUM_7,
		NUM_8,
		NUM_9,
		NUM_0,

		COUNT_
	};

	enum class MouseButton
	{
		LEFT,
		MIDDLE,
		RIGHT,

		COUNT_
	};

	bool IsKeyDown(Key in_key);
	bool IsMouseButtonDown(MouseButton in_button);

	// Motion accumulated since the previous call (in pixels) so exactly one caller per frame.
	Vec2 ConsumeMouseDelta();

	// Whether a UI is currently taking input, so typing into a debug field does not also drive
	// the game. Recorded here rather than queried from the UI directly, because input is a
	// platform concern and must not know which UI happens to be on top.
	//
	// The queries do not gate IsKeyDown and IsMouseButtonDown, which stay honest reports of the
	// hardware. Whether a particular action should yield to the UI is the caller's decision -
	// starting a mouse look should, continuing one already underway should not
	void SetUiCapture(bool in_keyboard, bool in_mouse);

	bool IsUiCapturingKeyboard();
	bool IsUiCapturingMouse();
}
