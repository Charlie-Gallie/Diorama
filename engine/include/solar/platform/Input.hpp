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
}
