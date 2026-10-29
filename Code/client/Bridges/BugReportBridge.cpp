#include <Bridges/BugReportBridge.h>

#include <Breadcrumbs.h>
#include <BuildVersion.h>
#include <Diagnostics/Folders.h>
#include <Diagnostics/LogRing.h>
#include <Diagnostics/MessageTrail.h>
#include <Diagnostics/ReportPaths.h>
#include <Diagnostics/Snapshot.h>
#include <SaveLoadManager.h>

#include <chrono>
#include <format>
#include <fstream>
#include <optional>
#include <thread>

namespace
{
std::string FormatLog(const std::vector<Diagnostics::LogLine>& acLines)
{
    std::string text;
    for (const auto& cLine : acLines)
    {
        const auto cTime = std::chrono::system_clock::time_point(std::chrono::milliseconds(cLine.TimeMs));
        text += std::format("{} {:%H:%M:%S} [{}] {}\n", cLine.TimeMs, std::chrono::floor<std::chrono::milliseconds>(cTime), cLine.Level, cLine.Text);
    }
    return text;
}

// The newest save in the folder (.ess), with its SKSE co-save beside it.
std::optional<std::filesystem::path> NewestSave(const std::filesystem::path& acSaves) noexcept
{
    std::error_code error;
    std::optional<std::filesystem::path> newest;
    std::filesystem::file_time_type newestTime{};
    for (const auto& cEntry : std::filesystem::directory_iterator(acSaves, error))
    {
        if (cEntry.path().extension() != ".ess")
            continue;
        const auto cTime = cEntry.last_write_time(error);
        if (!newest || cTime > newestTime)
            newest = cEntry.path(), newestTime = cTime;
    }
    return newest;
}

void WriteText(const std::filesystem::path& acPath, const std::string& acText) noexcept
{
    std::ofstream(acPath, std::ios::binary).write(acText.data(), static_cast<std::streamsize>(acText.size()));
}
} // namespace

std::string BugReportBridge::Write(const BugReportPart& acPart) noexcept
{
    const auto cSaves = BGSSaveLoadManager::SaveFilePath("report").parent_path();
    const auto cFolder = Diagnostics::PartFolder(Diagnostics::Folders::Reports(), acPart.Id, acPart.Host, acPart.Player);

    // Our last engine changes into the log first, so log.txt has them.
    Breadcrumbs::Dump();
    std::string snapshot = Diagnostics::Report().dump(1);
    std::string log = FormatLog(Diagnostics::PluginLog().Since());
    std::string messages = Diagnostics::FormatMessages(Diagnostics::Messages().Since());
    const auto cNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::string note = std::format("id: {}\nby: {}\ntext: {}\ngame: {} ({})\nbuild: {}\nunix_ms: {}\nsession_tick: {}\n", acPart.Id, acPart.By,
                                   acPart.Text, acPart.Player, acPart.Host ? "host" : "guest", BuildCommit(), cNowMs, acPart.SessionTick);
    const auto cSave = acPart.Host ? NewestSave(cSaves) : std::nullopt;
    const auto cReports = Diagnostics::Folders::Reports();

    // Now, not on the writer: Wine names only a path whose folders exist (ForPlayer).
    std::error_code created;
    std::filesystem::create_directories(cFolder, created);
    std::thread([cFolder, cReports, snapshot = std::move(snapshot), log = std::move(log), messages = std::move(messages), note = std::move(note), cSave] {
        std::error_code error;
        WriteText(cFolder / "note.txt", note);
        WriteText(cFolder / "log.txt", log);
        WriteText(cFolder / "messages.txt", messages);
        WriteText(cFolder / "snapshot.json", snapshot);
        // Kept once in reports/saves however many reports name it: an autosave or quicksave is overwritten later.
        if (cSave)
        {
            const auto cTime = std::filesystem::last_write_time(*cSave, error);
            const std::string cStored = Diagnostics::StoredSaveName(cSave->stem().string(), cTime.time_since_epoch().count());
            std::filesystem::create_directories(cReports / "saves", error);
            for (const char* cpExtension : {".ess", ".skse"})
            {
                const auto cFrom = std::filesystem::path(*cSave).replace_extension(cpExtension);
                const auto cTo = cReports / "saves" / (cStored + cpExtension);
                if (std::filesystem::exists(cFrom, error) && !std::filesystem::exists(cTo, error))
                    std::filesystem::copy_file(cFrom, cTo, error);
            }
            WriteText(cFolder / "save.txt", "saves/" + cStored + ".ess\n");
        }
        spdlog::info("Bug report written: {}", cFolder.string());
    }).detach();
    return Diagnostics::Folders::ForPlayer(cFolder);
}

void BugReportBridge::Export(const std::string& acPlayer, std::function<void(const std::string&)> aDone) noexcept
{
    std::thread([acPlayer, aDone = std::move(aDone)] {
        const auto cZip = Diagnostics::Folders::ExportNow(acPlayer);
        const std::string cShown = cZip.empty() ? std::string() : Diagnostics::Folders::ForPlayer(cZip);
        if (!cShown.empty())
            Diagnostics::Folders::CopyToClipboard(cShown);
        aDone(cShown);
    }).detach();
}
