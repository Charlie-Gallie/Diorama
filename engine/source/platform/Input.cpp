#include "solar/platform/Input.hpp"

#include <SDL3/SDL.h>

namespace solar::input
{
	namespace
	{
		SDL_Scancode ToScancode(Key in_key)
		{
			switch (in_key)
			{
			case Key::W:          return SDL_SCANCODE_W;
			case Key::A:          return SDL_SCANCODE_A;
			case Key::S:          return SDL_SCANCODE_S;
			case Key::D:          return SDL_SCANCODE_D;
			case Key::Q:          return SDL_SCANCODE_Q;
			case Key::E:          return SDL_SCANCODE_E;
			case Key::LEFT_SHIFT: return SDL_SCANCODE_LSHIFT;
			case Key::NUM_1:      return SDL_SCANCODE_1;
			case Key::NUM_2:      return SDL_SCANCODE_2;
			case Key::NUM_3:      return SDL_SCANCODE_3;
			case Key::NUM_4:      return SDL_SCANCODE_4;
			case Key::NUM_5:      return SDL_SCANCODE_5;
			case Key::NUM_6:      return SDL_SCANCODE_6;
			case Key::NUM_7:      return SDL_SCANCODE_7;
			case Key::NUM_8:      return SDL_SCANCODE_8;
			case Key::NUM_9:      return SDL_SCANCODE_9;
			case Key::NUM_0:      return SDL_SCANCODE_0;
			case Key::COUNT_:     break;
			}

			return SDL_SCANCODE_UNKNOWN;
		}

		SDL_MouseButtonFlags ToButtonMask(MouseButton in_button)
		{
			switch (in_button)
			{
			case MouseButton::LEFT:   return SDL_BUTTON_LMASK;
			case MouseButton::MIDDLE: return SDL_BUTTON_MMASK;
			case MouseButton::RIGHT:  return SDL_BUTTON_RMASK;
			case MouseButton::COUNT_: break;
			}

			return 0;
		}
	}

	bool IsKeyDown(Key in_key)
	{
		const bool* keyStates = SDL_GetKeyboardState(nullptr);

		return keyStates[ToScancode(in_key)];
	}

	bool IsMouseButtonDown(MouseButton in_button)
	{
		return (SDL_GetMouseState(nullptr, nullptr) & ToButtonMask(in_button)) != 0;
	}

	Vec2 ConsumeMouseDelta()
	{
		float deltaX = 0.0f;
		float deltaY = 0.0f;

		SDL_GetRelativeMouseState(&deltaX, &deltaY);

		return { deltaX, deltaY };
	}
}
