#pragma once

// Tracy, when built with the profiler option (COOP_PROFILER; docs/design/testing.md, Profiling). Idle until a
// profiler connects. Zones and plots only inside our update (World::Update, run from the VM update's job), never in
// hooks: Profile::Stop runs between frames before a hot reload unloads us.
#if COOP_PROFILER
#include <tracy/Tracy.hpp>
#define PROFILE_ZONE(acpName) ZoneScopedN(acpName)
#define PROFILE_FRAME() FrameMark
#define PROFILE_PLOT(acpName, aValue) TracyPlot(acpName, aValue)
#define PROFILE_PLOT_CONFIG(acpName, aFormat) TracyPlotConfig(acpName, aFormat, true, true, 0)
#else
#define PROFILE_ZONE(acpName)
#define PROFILE_FRAME()
#define PROFILE_PLOT(acpName, aValue)
#define PROFILE_PLOT_CONFIG(acpName, aFormat)
#endif

namespace Profile
{
// Before our hooks run (manual lifetime: no Tracy in DllMain) and after they are removed.
void Start() noexcept;
void Stop() noexcept;
// Once a frame from World::Update: hook call rates, network and memory plots.
void PlotFrame() noexcept;
} // namespace Profile
