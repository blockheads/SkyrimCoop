#include "LogTrail.h"

#include <spdlog/sinks/base_sink.h>

#include <deque>
#include <mutex>

namespace TestHarness
{
namespace
{
constexpr size_t cKept = 4000;

struct Line
{
    uint64_t Seq;
    int64_t TimeMs; // since the Unix epoch, as the scenario's clock
    std::string Level;
    std::string Text;
};

std::mutex s_lock;
std::deque<Line> s_lines;
uint64_t s_next = 0;

// Called on every thread that logs.
struct TrailSink final : spdlog::sinks::base_sink<spdlog::details::null_mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& acMessage) override
    {
        if (acMessage.level < spdlog::level::info)
            return;
        const auto cMs = std::chrono::duration_cast<std::chrono::milliseconds>(acMessage.time.time_since_epoch()).count();
        const auto cLevel = spdlog::level::to_string_view(acMessage.level);
        std::scoped_lock _{s_lock};
        s_lines.push_back({s_next++, cMs, std::string(cLevel.data(), cLevel.size()), std::string(acMessage.payload.data(), acMessage.payload.size())});
        if (s_lines.size() > cKept)
            s_lines.pop_front();
    }
    void flush_() override {}
};

// spdlog's sink list is not safe to change while others log: a copy of the logger with the trail joins it, and the
// one it replaces is kept, never freed, for the threads still inside it.
std::shared_ptr<spdlog::logger> s_replaced;
} // namespace

void LogTrail::Poll() noexcept
{
    if (s_replaced)
        return;
    auto pDefault = spdlog::default_logger();
    if (!pDefault || pDefault->sinks().empty())
        return;
    auto sinks = pDefault->sinks();
    sinks.push_back(std::make_shared<TrailSink>());
    auto pLogger = std::make_shared<spdlog::logger>(pDefault->name(), sinks.begin(), sinks.end());
    pLogger->set_level(pDefault->level());
    pLogger->flush_on(pDefault->flush_level());
    s_replaced = pDefault;
    spdlog::set_default_logger(pLogger);
}

nlohmann::json LogTrail::Since(uint64_t aSince) const
{
    auto lines = nlohmann::json::array();
    std::scoped_lock _{s_lock};
    for (const auto& cLine : s_lines)
        if (cLine.Seq >= aSince)
            lines.push_back({{"seq", cLine.Seq}, {"time_ms", cLine.TimeMs}, {"level", cLine.Level}, {"text", cLine.Text}});
    return {{"ok", true}, {"lines", lines}, {"next", s_next}};
}
} // namespace TestHarness
