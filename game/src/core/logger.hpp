#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>

namespace mc::log {

// Initialize the global logger. Idempotent. Call once at startup.
// Console sink + rotating file sink ("minekampf.log").
inline void init() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [%t] %v");
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("minekampf.log", true);
    file_sink->set_level(spdlog::level::trace);
    auto logger = std::make_shared<spdlog::logger>("mc", spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

inline void shutdown() { spdlog::shutdown(); }

} // namespace mc::log

#define MC_LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define MC_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define MC_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define MC_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define MC_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
