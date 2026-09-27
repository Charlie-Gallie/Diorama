#pragma once

#include <cstdint>

#include "solar/core/Error.hpp"

namespace solar
{
	class Platform
	{
	public:
		enum class Subsystem : uint32_t
		{
			VIDEO = 1 << 0,
			AUDIO = 1 << 1,
			GAMEPAD = 1 << 2
		};


		Platform() = default;
		~Platform();

		Platform(const Platform&) = delete;
		Platform& operator=(const Platform&) = delete;

		[[nodiscard]] Error Initialize(Subsystem in_subsystem);

	private:
		bool isInitialized = false;
	};

	constexpr Platform::Subsystem operator|(Platform::Subsystem in_a, Platform::Subsystem in_b)
	{
		return static_cast<Platform::Subsystem>(static_cast<uint32_t>(in_a) | static_cast<uint32_t>(in_b));
	}

	constexpr Platform::Subsystem operator&(Platform::Subsystem in_a, Platform::Subsystem in_b)
	{
		return static_cast<Platform::Subsystem>(static_cast<uint32_t>(in_a) & static_cast<uint32_t>(in_b));
	}

	constexpr bool HasFlag(Platform::Subsystem in_value, Platform::Subsystem in_flag)
	{
		return (in_value & in_flag) == in_flag;
	}
}