// Bug reports' pieces with no engine (docs/design/bug-reports.md): the log ring, the message trail, names, zips, exports.
#include <catch2/catch.hpp>

#include <Diagnostics/CrashReports.h>
#include <Diagnostics/Export.h>
#include <Diagnostics/LogRing.h>
#include <Diagnostics/MessageTrail.h>
#include <Diagnostics/ReportPaths.h>
#include <Diagnostics/Zip.h>

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <random>

using namespace Diagnostics;

TEST_CASE("The log ring keeps the newest lines, by count and by age", "[diagnostics]")
{
    LogRing ring(3, 1000);
    for (int i = 0; i < 5; ++i)
        ring.Add({0, i, "info", std::to_string(i)});
    auto lines = ring.Since(0);
    REQUIRE(lines.size() == 3);
    CHECK(lines.front().Text == "2");
    CHECK(lines.back().Seq == 4);
    CHECK(ring.Next() == 5);
    CHECK(ring.Since(4).size() == 1);

    ring.Add({0, 2000, "warning", "late"});
    lines = ring.Since(0);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].Text == "late");
}

TEST_CASE("The message trail keeps the newest messages and writes one line each", "[diagnostics]")
{
    MessageTrail trail(2, 60000);
    trail.Add({1, 10, MessageWay::Sent, "BugReportRequest", 0, 20});
    trail.Add({2, 11, MessageWay::Received, "NotifyBugReport", 0, 40});
    trail.Add({3, 12, MessageWay::HostOut, "NotifyBugReport", 0x2a, 40});
    const auto cAll = trail.Since();
    REQUIRE(cAll.size() == 2);
    CHECK(cAll[0].TimeMs == 2);
    CHECK(FormatMessages(cAll) == "2 t=11 R NotifyBugReport peer=0 40B\n3 t=12 O NotifyBugReport peer=2a 40B\n");
}

TEST_CASE("Report ids and folders are file names", "[diagnostics]")
{
    std::tm local{};
    local.tm_year = 126;
    local.tm_mon = 9;
    local.tm_mday = 29;
    local.tm_hour = 22;
    local.tm_min = 14;
    CHECK(ReportId(local, 0x1a3f) == "2026-10-29-2214-a3f");
    CHECK(SafeName("Ulfric Storm/cloak") == "Ulfric_Storm_cloak");
    CHECK(SafeName("..") == "player");
    CHECK(SafeName("") == "player");
    CHECK(PartFolder("reports", "2026-10-29-2214-a3f", true, "Lydia") == std::filesystem::path("reports/2026-10-29-2214-a3f/host-Lydia"));
    CHECK(PartFolder("reports", "../x", false, "") == std::filesystem::path("reports/___x/guest-player"));
    CHECK(StoredSaveName("Save 3 - Lydia", 0x1d9f) == "Save_3_-_Lydia-1d9f");
}

namespace
{
// A fresh folder under the system's temp folder, removed after the test.
struct TempFolder
{
    TempFolder()
        : Path(std::filesystem::temp_directory_path() / ("coop-diagnostics-" + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(Path);
    }
    ~TempFolder() { std::filesystem::remove_all(Path); }
    std::filesystem::path Path;
};

void WriteFile(const std::filesystem::path& acPath, const std::string& acText)
{
    std::filesystem::create_directories(acPath.parent_path());
    std::ofstream(acPath, std::ios::binary) << acText;
}

// Each entry of a zip (names and inflated data), read through its central directory.
std::map<std::string, std::string> ReadZip(const std::filesystem::path& acZip)
{
    std::ifstream in(acZip, std::ios::binary);
    const std::string cZip((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto u16 = [&](size_t i) { return uint32_t(uint8_t(cZip[i])) | uint32_t(uint8_t(cZip[i + 1])) << 8; };
    const auto u32 = [&](size_t i) { return u16(i) | u16(i + 2) << 16; };
    const size_t cEnd = cZip.size() - 22;
    REQUIRE(u32(cEnd) == 0x06054b50);
    std::map<std::string, std::string> entries;
    size_t at = u32(cEnd + 16);
    for (uint32_t i = 0; i < u16(cEnd + 10); ++i)
    {
        REQUIRE(u32(at) == 0x02014b50);
        const uint32_t cCompressed = u32(at + 20), cSize = u32(at + 24), cNameLength = u16(at + 28), cOffset = u32(at + 42);
        const std::string cName = cZip.substr(at + 46, cNameLength);
        const size_t cData = cOffset + 30 + u16(cOffset + 26) + u16(cOffset + 28);
        std::string data(cSize, '\0');
        z_stream stream{};
        inflateInit2(&stream, -15);
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(cZip.data() + cData));
        stream.avail_in = cCompressed;
        stream.next_out = reinterpret_cast<Bytef*>(data.data());
        stream.avail_out = cSize;
        CHECK(inflate(&stream, Z_FINISH) == Z_STREAM_END);
        inflateEnd(&stream);
        entries[cName] = data;
        at += 46 + cNameLength + u16(at + 30) + u16(at + 32);
    }
    return entries;
}
} // namespace

TEST_CASE("A zip holds each file as written", "[diagnostics]")
{
    TempFolder folder;
    WriteFile(folder.Path / "log.txt", std::string(10000, 'x') + "end");
    ZipWriter zip(folder.Path / "out.zip");
    REQUIRE(zip.IsOpen());
    CHECK(zip.AddBytes("info.txt", "build: abc\n"));
    CHECK(zip.AddFile("logs/log.txt", folder.Path / "log.txt"));
    CHECK(zip.Finish());
    const auto cEntries = ReadZip(folder.Path / "out.zip");
    REQUIRE(cEntries.size() == 2);
    CHECK(cEntries.at("info.txt") == "build: abc\n");
    CHECK(cEntries.at("logs/log.txt") == std::string(10000, 'x') + "end");
}

TEST_CASE("An export takes the reports, logs, crash logs and dumps changed since the last", "[diagnostics]")
{
    TempFolder folder;
    const ExportSources cSources{folder.Path / "reports", folder.Path / "logs", folder.Path / "SKSE", folder.Path / "game"};
    WriteFile(cSources.Reports / "old" / "host-Lydia" / "note.txt", "old");
    WriteFile(cSources.PluginLogs / "tp_client.1.log", "rotated");
    const auto cSince = std::filesystem::last_write_time(cSources.PluginLogs / "tp_client.1.log");
    const auto cLater = cSince + std::chrono::seconds(5);
    for (const auto& [cPath, cText] : std::vector<std::pair<std::filesystem::path, std::string>>{
             {cSources.Reports / "2026-10-29-2214-a3f" / "guest-Ulfric" / "log.txt", "log"},
             {cSources.PluginLogs / "tp_client.log", "now"},
             {cSources.PluginLogs / "other.log", "not ours"},
             {cSources.CrashLogs / "crash-2026-10-29.log", "crash"},
             {cSources.Dumps / "crash_UTC_2026.dmp", "dump"},
             {cSources.Dumps / "SkyrimSE.exe", "game"}})
    {
        WriteFile(cPath, cText);
        std::filesystem::last_write_time(cPath, cLater);
    }

    auto files = ExportFiles(cSources, cSince);
    std::vector<std::string> names;
    for (const auto& cFile : files)
        names.push_back(cFile.second);
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"crash/crash-2026-10-29.log", "crash/crash_UTC_2026.dmp", "logs/tp_client.log",
                                            "reports/2026-10-29-2214-a3f/guest-Ulfric/log.txt"});
    CHECK(ExportFiles(cSources, std::nullopt).size() == 6);

    REQUIRE(WriteExport(folder.Path / "out" / "SkyrimCoop.zip", files, "build: abc\n"));
    const auto cEntries = ReadZip(folder.Path / "out" / "SkyrimCoop.zip");
    CHECK(cEntries.size() == 5);
    CHECK(cEntries.at("info.txt") == "build: abc\n");
    CHECK(cEntries.at("crash/crash-2026-10-29.log") == "crash");
}

TEST_CASE("A run without a clean exit, or with a crash log after it started, becomes a report at the next start", "[diagnostics]")
{
    TempFolder folder;
    const ExportSources cSources{folder.Path / "reports", folder.Path / "logs", folder.Path / "SKSE", folder.Path / "game"};
    const CrashReports::Marks cLast{folder.Path / "run", 100};
    const CrashReports::Marks cNext{folder.Path / "run", 200};
    WriteFile(cSources.PluginLogs / "tp_client.log", "last run");
    WriteFile(cSources.PluginLogs / "tp_client-client.log", "the other game's");

    SECTION("no run before: nothing")
    CHECK_FALSE(CrashReports::CollectLastRun(cNext, cSources, ""));

    SECTION("a clean exit: nothing; the same process (a hot reload): nothing")
    {
        CrashReports::MarkRunning(cLast);
        CHECK_FALSE(CrashReports::LastRunEndedBadly(cLast, cSources.CrashLogs));
        CrashReports::MarkCleanExit(cLast);
        CHECK_FALSE(CrashReports::LastRunEndedBadly(cNext, cSources.CrashLogs));
    }

    SECTION("no clean exit: its logs, no crash log")
    {
        CrashReports::MarkRunning(cLast);
        const auto cReport = CrashReports::CollectLastRun(cNext, cSources, "");
        REQUIRE(cReport);
        CHECK(std::filesystem::exists(*cReport / "tp_client.log"));
        CHECK_FALSE(std::filesystem::exists(*cReport / "tp_client-client.log"));
        std::ifstream note(*cReport / "note.txt");
        const std::string cNote((std::istreambuf_iterator<char>(note)), std::istreambuf_iterator<char>());
        CHECK(cNote.starts_with("kind: ended"));
    }

    SECTION("a crash log after a clean exit's mark (the crash handler exits): its crash log too")
    {
        CrashReports::MarkRunning(cLast);
        CrashReports::MarkCleanExit(cLast);
        const auto cStarted = std::filesystem::last_write_time(cLast.Folder / "last_run");
        WriteFile(cSources.CrashLogs / "crash-1.log", "access violation");
        std::filesystem::last_write_time(cSources.CrashLogs / "crash-1.log", cStarted + std::chrono::seconds(5));
        const auto cReport = CrashReports::CollectLastRun(cNext, cSources, "");
        REQUIRE(cReport);
        CHECK(std::filesystem::exists(*cReport / "crash-1.log"));
    }
}
