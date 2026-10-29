#include "Export.h"
#include "Zip.h"

namespace Diagnostics
{
namespace
{
bool Newer(const std::filesystem::path& acFile, std::optional<std::filesystem::file_time_type> aSince) noexcept
{
    std::error_code error;
    const auto cTime = std::filesystem::last_write_time(acFile, error);
    return !error && (!aSince || cTime > *aSince);
}

// The folder's files whose name matches, as <aPrefix>/<file name>.
void AddMatching(std::vector<ExportFile>& aFiles, const std::filesystem::path& acFolder, std::string_view aStart, std::string_view aEnd,
                 const std::string& acPrefix, std::optional<std::filesystem::file_time_type> aSince)
{
    std::error_code error;
    if (acFolder.empty())
        return;
    for (const auto& cEntry : std::filesystem::directory_iterator(acFolder, error))
    {
        const std::string cName = cEntry.path().filename().string();
        if (cEntry.is_regular_file(error) && cName.starts_with(aStart) && cName.ends_with(aEnd) && Newer(cEntry.path(), aSince))
            aFiles.emplace_back(cEntry.path(), acPrefix + "/" + cName);
    }
}
} // namespace

std::vector<ExportFile> ExportFiles(const ExportSources& acSources, std::optional<std::filesystem::file_time_type> aSince)
{
    std::vector<ExportFile> files;
    std::error_code error;
    if (!acSources.Reports.empty() && std::filesystem::is_directory(acSources.Reports, error))
        for (const auto& cEntry : std::filesystem::recursive_directory_iterator(acSources.Reports, error))
            if (cEntry.is_regular_file(error) && Newer(cEntry.path(), aSince))
                files.emplace_back(cEntry.path(), "reports/" + std::filesystem::relative(cEntry.path(), acSources.Reports, error).generic_string());
    AddMatching(files, acSources.PluginLogs, "tp_client", ".log", "logs", aSince);
    AddMatching(files, acSources.CrashLogs, "crash-", ".log", "crash", aSince);
    AddMatching(files, acSources.Dumps, "crash_", ".dmp", "crash", aSince);
    return files;
}

bool WriteExport(const std::filesystem::path& acZip, const std::vector<ExportFile>& acFiles, const std::string& acInfo) noexcept
{
    std::error_code error;
    std::filesystem::create_directories(acZip.parent_path(), error);
    ZipWriter zip(acZip);
    if (!zip.IsOpen())
        return false;
    zip.AddBytes("info.txt", acInfo);
    for (const auto& [cFile, cName] : acFiles)
        zip.AddFile(cName, cFile); // a log still being written may be missing a line, never the zip
    return zip.Finish();
}
} // namespace Diagnostics
