#pragma once

#include <spdlog/sinks/sink.h>

#include "Trail.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Diagnostics
{
struct LogLine
{
    uint64_t Seq;
    int64_t TimeMs; // since the Unix epoch
    std::string Level;
    std::string Text;
};

// The plugin's log lines: a bug report's log.txt and the harness's log_trail.
using LogRing = Trail<LogLine>;
// The plugin's log, the last 10 minutes (info and up).
LogRing& PluginLog() noexcept;
// A sink writing into the plugin's ring, for the default logger (Plugin.cpp).
std::shared_ptr<spdlog::sinks::sink> MakePluginLogSink();
} // namespace Diagnostics
