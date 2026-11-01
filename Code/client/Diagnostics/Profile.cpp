#include "Profile.h"

#if COOP_PROFILER
#include <Breadcrumbs.h>
#include <Services/TransportService.h>
#include <World.h>

#include <psapi.h>

#include <unordered_map>
#endif

namespace Profile
{
#if COOP_PROFILER
void Start() noexcept
{
    tracy::StartupProfiler();
    PROFILE_PLOT_CONFIG("hook calls", tracy::PlotFormatType::Number);
    PROFILE_PLOT_CONFIG("memory working set", tracy::PlotFormatType::Memory);
    PROFILE_PLOT_CONFIG("line sent", tracy::PlotFormatType::Memory);
    PROFILE_PLOT_CONFIG("line received", tracy::PlotFormatType::Memory);
}

void Stop() noexcept
{
    tracy::ShutdownProfiler();
}

void PlotFrame() noexcept
{
    if (!TracyIsConnected)
        return;
    // Each hook's calls this frame (breadcrumbs count them all), and their sum. Channel names are literals: plot names.
    static std::unordered_map<const char*, uint64_t> s_previous;
    const char* what[512];
    uint64_t total[512];
    int64_t sum = 0;
    for (size_t i = 0, cCount = Breadcrumbs::Totals(what, total, 512); i < cCount; ++i)
    {
        uint64_t& previous = s_previous[what[i]];
        const auto cCalls = static_cast<int64_t>(total[i] - previous);
        if (previous)
        {
            TracyPlot(what[i], cCalls);
            sum += cCalls;
        }
        previous = total[i];
    }
    PROFILE_PLOT("hook calls", sum);

    static uint32_t s_frame = 0;
    if (++s_frame % 30)
        return;
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
        PROFILE_PLOT("memory working set", static_cast<int64_t>(memory.WorkingSetSize));
    // A guest's line over the last second (the host's own route counts nothing).
    const auto cLine = World::Get().GetTransport().GetStatistics();
    PROFILE_PLOT("line sent", static_cast<int64_t>(cLine.SentBytes));
    PROFILE_PLOT("line received", static_cast<int64_t>(cLine.RecvBytes));
}
#else
void Start() noexcept
{
}

void Stop() noexcept
{
}

void PlotFrame() noexcept
{
}
#endif
} // namespace Profile
