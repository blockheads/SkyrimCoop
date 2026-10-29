#pragma once

#include "Export.h"

#include <cstdint>
#include <filesystem>
#include <optional>

// A run that did not end cleanly (a crash, a freeze the player ended, the game killed), found at the next start:
// its logs and crash log become a report (docs/design/bug-reports.md). No engine.
namespace Diagnostics::CrashReports
{
// Marks under acFolder: last_run (this process, at start) and clean_exit (at a clean exit).
struct Marks
{
    std::filesystem::path Folder;
    uint32_t Process{};
};

// Whether the last run ended without a clean exit or with a crash log after it started. Not this process's own
// (a hot reload starts the plugin again).
[[nodiscard]] bool LastRunEndedBadly(const Marks& acMarks, const std::filesystem::path& acCrashLogs) noexcept;
// The last run's report: <reports>/<id>-crash/ with its plugin logs, its crash log if any and note.txt. Before this
// run's log opens. None when the last run ended cleanly.
std::optional<std::filesystem::path> CollectLastRun(const Marks& acMarks, const ExportSources& acSources, std::string_view aInstance) noexcept;
void MarkRunning(const Marks& acMarks) noexcept;
void MarkCleanExit(const Marks& acMarks) noexcept;
} // namespace Diagnostics::CrashReports
