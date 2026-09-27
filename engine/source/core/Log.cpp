#include "solar/core/Log.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <memory>
#include <vector>

namespace solar::log
{
	namespace
	{
		constexpr size_t categoryCount = static_cast<size_t>(Category::COUNT_);

		// One logger per category but all using the same sink
		// This means we get an interleaved output in the right order
		std::array<std::shared_ptr<spdlog::logger>, categoryCount> loggers;

		// For devtools console panel to read recent messages back
		std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ringBufferSink;

		constexpr const char* CategoryName(Category in_category)
		{
			switch (in_category)
			{
			case Category::CORE:
				return "CORE";

			case Category::PLATFORM:
				return "PLATFORM";

			case Category::RENDER:
				return "RENDER";

			default:
				return "UNKNOWN";
			}
		}

		constexpr spdlog::level::level_enum ToSpdlogLevel(Level in_level)
		{
			switch (in_level)
			{
			case Level::TRACE:
				return spdlog::level::trace;

			case Level::INFO:
				return spdlog::level::info;

			case Level::WARN:
				return spdlog::level::warn;

			case Level::ERR:
				return spdlog::level::err;

			case Level::FATAL:
				return spdlog::level::critical;
			}

			return spdlog::level::info;
		}
	}

	void Initialize()
	{
		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

		// %n is the logger name, which equates to the log::Category
		// %^ and %$ bracket the region the console sink colours
		// %s is the source file basename and %# its line - only emitted when
		// Location is populated, which it is not in Shipping
#if SOLAR_LOG_LOCATION_ENABLED
		constexpr const char* consolePattern = "%^[%T.%e] %n [%l]%$ %s:%#: %v";
		constexpr const char* bufferPattern = "[%T.%e] %n [%l] %s:%#: %v";
#else
		constexpr const char* consolePattern = "%^[%T.%e] %n [%l]%$: %v";
		constexpr const char* bufferPattern = "[%T.%e] %n [%l]: %v";
#endif

		consoleSink->set_pattern(consolePattern);

		ringBufferSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(2048);
		ringBufferSink->set_pattern(bufferPattern);

		const std::vector<spdlog::sink_ptr> sinks = { consoleSink, ringBufferSink };

		for (size_t index = 0; index < categoryCount; ++index)
		{
			auto logger = std::make_shared<spdlog::logger>(
				CategoryName(static_cast<Category>(index)),
				sinks.begin(),
				sinks.end()
			);

			logger->set_level(spdlog::level::trace);

			// Errors need to be flushed immediately so that we can get the cause of crashes
			logger->flush_on(spdlog::level::err);

			loggers[index] = std::move(logger);
		}
	}

	void Shutdown()
	{
		for (auto& logger : loggers)
		{
			if (logger != nullptr)
			{
				logger->flush();
			}

			logger.reset();
		}

		ringBufferSink.reset();

		spdlog::shutdown();
	}

	void SetLevel(Category in_category, Level in_level)
	{
		auto& logger = loggers[static_cast<size_t>(in_category)];

		if (logger != nullptr)
		{
			logger->set_level(ToSpdlogLevel(in_level));
		}
	}

	void Write(Category in_category, Level in_level, const Location& in_location, std::string_view in_text)
	{
		auto& logger = loggers[static_cast<size_t>(in_category)];

		if (logger == nullptr)
		{
			return; // Logged before Initialize, or after Shutdown.
		}

		const spdlog::source_loc sourceLocation{
			in_location.file != nullptr ? in_location.file : "",
			in_location.line,
			""
		};

		logger->log(sourceLocation, ToSpdlogLevel(in_level), in_text);
	}
}
