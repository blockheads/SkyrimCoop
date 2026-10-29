#include "CrashReports.h"
#include "ReportPaths.h"

#include <format>
#include <fstream>
#include <sstream>

namespace Diagnostics::CrashReports
{
namespace
{
std::optional<std::filesystem::file_time_type> TimeOf(const std::filesystem::path& acFile) noexcept
{
    std::error_code error;
    const auto cTime = std::filesystem::last_write_time(acFile, error);
    return error ? std::nullopt : std::optional(cTime);
}

std::string Read(const std::filesystem::path& acFile) noexcept
{
    std::ifstream in(acFile, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The newest crash log written after aSince.
std::optional<std::filesystem::path> CrashLogAfter(const std::filesystem::path& acCrashLogs, std::filesystem::file_time_type aSince) noexcept
{
    std::optional<std::filesystem::path> newest;
    std::filesystem::file_time_type newestTime = aSince;
    std::error_code error;
    if (acCrashLogs.empty())
        return newest;
    for (const auto& cEntry : std::filesystem::directory_iterator(acCrashLogs, error))
    {
        const std::string cName = cEntry.path().filename().string();
        const auto cTime = TimeOf(cEntry.path());
        if (cName.starts_with("crash-") && cName.ends_with(".log") && cTime && *cTime > newestTime)
            newest = cEntry.path(), newestTime = *cTime;
    }
    return newest;
}
} // namespace

bool LastRunEndedBadly(const Marks& acMarks, const std::filesystem::path& acCrashLogs) noexcept
{
    const auto cStarted = TimeOf(acMarks.Folder / "last_run");
    if (!cStarted || Read(acMarks.Folder / "last_run") == std::to_string(acMarks.Process))
        return false;
    const auto cClean = TimeOf(acMarks.Folder / "clean_exit");
    return !cClean || *cClean < *cStarted || CrashLogAfter(acCrashLogs, *cStarted);
}

std::optional<std::filesystem::path> CollectLastRun(const Marks& acMarks, const ExportSources& acSources, std::string_view aInstance) noexcept
{
    if (!LastRunEndedBadly(acMarks, acSources.CrashLogs))
        return std::nullopt;
    const auto cStarted = *TimeOf(acMarks.Folder / "last_run");
    const auto cFolder = acSources.Reports / (NewReportId() + "-crash");
    std::error_code error;
    std::filesystem::create_directories(cFolder, error);
    const auto cCrashLog = CrashLogAfter(acSources.CrashLogs, cStarted);
    if (cCrashLog)
        std::filesystem::copy_file(*cCrashLog, cFolder / cCrashLog->filename(), std::filesystem::copy_options::overwrite_existing, error);
    // This game's plugin logs, the rotated ones too (tp_client<instance>.log, .1.log, ...).
    const std::string cLog = std::format("tp_client{}", aInstance);
    for (const auto& cEntry : std::filesystem::directory_iterator(acSources.PluginLogs, error))
    {
        const std::string cName = cEntry.path().filename().string();
        if (cName.starts_with(cLog + ".") && cName.ends_with(".log"))
            std::filesystem::copy_file(cEntry.path(), cFolder / cName, std::filesystem::copy_options::overwrite_existing, error);
    }
    std::ofstream(cFolder / "note.txt") << std::format("kind: {}\ncrash_log: {}\n",
                                                      cCrashLog ? "crash" : "ended (no crash log: a freeze ended by the player, or the game was closed)",
                                                      cCrashLog ? cCrashLog->filename().string() : "none");
    return cFolder;
}

void MarkRunning(const Marks& acMarks) noexcept
{
    std::error_code error;
    std::filesystem::create_directories(acMarks.Folder, error);
    std::ofstream(acMarks.Folder / "last_run", std::ios::binary | std::ios::trunc) << acMarks.Process;
}

void MarkCleanExit(const Marks& acMarks) noexcept
{
    std::ofstream(acMarks.Folder / "clean_exit", std::ios::binary | std::ios::trunc) << acMarks.Process;
}
} // namespace Diagnostics::CrashReports
