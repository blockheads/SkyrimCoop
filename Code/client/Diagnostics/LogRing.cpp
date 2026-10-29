#include "LogRing.h"

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

namespace Diagnostics
{
LogRing& PluginLog() noexcept
{
    static LogRing s_ring(20000, 10 * 60 * 1000);
    return s_ring;
}

namespace
{
// Called on every thread that logs; the ring locks itself.
struct RingSink final : spdlog::sinks::base_sink<spdlog::details::null_mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& acMessage) override
    {
        if (acMessage.level < spdlog::level::info)
            return;
        const auto cMs = std::chrono::duration_cast<std::chrono::milliseconds>(acMessage.time.time_since_epoch()).count();
        const auto cLevel = spdlog::level::to_string_view(acMessage.level);
        PluginLog().Add({0, cMs, std::string(cLevel.data(), cLevel.size()), std::string(acMessage.payload.data(), acMessage.payload.size())});
    }
    void flush_() override {}
};
} // namespace

std::shared_ptr<spdlog::sinks::sink> MakePluginLogSink()
{
    return std::make_shared<RingSink>();
}
} // namespace Diagnostics
