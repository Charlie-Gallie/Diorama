#pragma once

#include <format>
#include <string_view>

// Source location is compiled out in Shipping: __FILE__ embeds the build
// machine's absolute path, which should not reach a released binary.
// clang-format off
#if SOLAR_LOG_LOCATION_ENABLED
	#define SOLAR_LOG_LOCATION solar::log::Location{ __FILE__, __LINE__ }
#else
	#define SOLAR_LOG_LOCATION solar::log::Location{}
#endif
// clang-format on

// Trace expands to nothing in Shipping, so its arguments are never evaluated.
// A function could only discard the result after doing the formatting work.
#if SOLAR_LOG_TRACE_ENABLED
	#define SOLAR_LOG_TRACE(category, ...) \
		solar::log::Format(solar::log::Category::category, solar::log::Level::TRACE, SOLAR_LOG_LOCATION, __VA_ARGS__)
#else
	#define SOLAR_LOG_TRACE(category, ...) ((void)0)
#endif

#define SOLAR_LOG_INFO(category, ...) \
	solar::log::Format(solar::log::Category::category, solar::log::Level::INFO, SOLAR_LOG_LOCATION, __VA_ARGS__)

#define SOLAR_LOG_WARN(category, ...) \
	solar::log::Format(solar::log::Category::category, solar::log::Level::WARN, SOLAR_LOG_LOCATION, __VA_ARGS__)

#define SOLAR_LOG_ERR(category, ...) \
	solar::log::Format(solar::log::Category::category, solar::log::Level::ERR, SOLAR_LOG_LOCATION, __VA_ARGS__)

#define SOLAR_LOG_FATAL(category, ...) \
	solar::log::Format(solar::log::Category::category, solar::log::Level::FATAL, SOLAR_LOG_LOCATION, __VA_ARGS__)

namespace solar::log
{
	enum class Category
	{
		CORE,
		PLATFORM,
		RENDER,

		COUNT_ // Used for the logger array size
	};

	enum class Level
	{
		TRACE,
		INFO,
		WARN,
		ERR,
		FATAL
	};

	struct Location
	{
		const char* file = nullptr;
		int line = 0;
	};

	void Initialize();
	void Shutdown();

	// Per-category minimum level to discard lower level messages
	void SetLevel(Category in_category, Level in_level);

	void Write(Category in_category, Level in_level, const Location& in_location, std::string_view in_text);

	template <typename... Args>
	void Format(
		Category in_category,
		Level in_level,
		const Location& in_location,
		std::format_string<Args...> in_format,
		Args&&... in_args
	)
	{
		Write(in_category, in_level, in_location, std::format(in_format, std::forward<Args>(in_args)...));
	}
}
