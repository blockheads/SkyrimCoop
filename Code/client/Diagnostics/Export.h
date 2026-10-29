#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Everything a player sends after a session (docs/design/bug-reports.md): bug reports, crash bundles, logs, crash
// logs and dumps, in one zip. No engine.
namespace Diagnostics
{
struct ExportSources
{
    std::filesystem::path Reports;    // My Games/Skyrim Special Edition/SkyrimCoop/reports
    std::filesystem::path PluginLogs; // Data/SKSE/Plugins/logs (tp_client*.log)
    std::filesystem::path CrashLogs;  // My Games/Skyrim Special Edition/SKSE (CrashLoggerSSE's crash-*.log)
    std::filesystem::path Dumps;      // the game's folder (our crash_*.dmp)
};

using ExportFile = std::pair<std::filesystem::path, std::string>; // the file, its name in the zip

// The files changed after aSince (all of them without), each with its name in the zip.
[[nodiscard]] std::vector<ExportFile> ExportFiles(const ExportSources& acSources, std::optional<std::filesystem::file_time_type> aSince);
// Zips the files and info.txt; false if it could not.
bool WriteExport(const std::filesystem::path& acZip, const std::vector<ExportFile>& acFiles, const std::string& acInfo) noexcept;
} // namespace Diagnostics
