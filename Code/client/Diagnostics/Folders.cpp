#include "Folders.h"
#include "ReportPaths.h"

#include <BuildVersion.h>
#include <PluginPaths.h>

#include <ShlObj.h>

#include <format>
#include <fstream>
#include <sstream>

namespace Diagnostics::Folders
{
namespace
{
std::filesystem::path KnownFolder(const KNOWNFOLDERID& acId) noexcept
{
    PWSTR pPath = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(acId, 0, nullptr, &pPath)))
        path = pPath;
    CoTaskMemFree(pPath);
    return path;
}

std::filesystem::path MyGames() noexcept
{
    return KnownFolder(FOLDERID_Documents) / "My Games" / "Skyrim Special Edition";
}

std::string ReadText(const std::filesystem::path& acFile) noexcept
{
    std::ifstream in(acFile, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
} // namespace

std::filesystem::path Coop() noexcept
{
    return MyGames() / "SkyrimCoop";
}

std::filesystem::path Reports() noexcept
{
    return Coop() / "reports";
}

ExportSources Sources() noexcept
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return {Reports(), PluginPaths::GetDirectory() / "logs", MyGames() / "SKSE", std::filesystem::path(exe).parent_path()};
}

CrashReports::Marks RunMarks() noexcept
{
    return {Coop() / ("run" + PluginPaths::InstanceSuffix()), static_cast<uint32_t>(GetCurrentProcessId())};
}

std::string ForPlayer(const std::filesystem::path& acPath) noexcept
{
    // Wine's own export: the host system's path of a Windows one.
    using TUnixName = char*(__cdecl*)(LPCWSTR);
    const auto pUnixName = reinterpret_cast<TUnixName>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name"));
    if (pUnixName)
        if (char* pName = pUnixName(acPath.c_str()))
        {
            std::string name = pName;
            HeapFree(GetProcessHeap(), 0, pName);
            return name;
        }
    return acPath.string();
}

void CopyToClipboard(const std::string& acText) noexcept
{
    const std::wstring cText = std::filesystem::path(acText).wstring();
    if (!OpenClipboard(nullptr))
        return;
    EmptyClipboard();
    if (HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, (cText.size() + 1) * sizeof(wchar_t)))
    {
        std::memcpy(GlobalLock(hText), cText.c_str(), (cText.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(hText);
        if (!SetClipboardData(CF_UNICODETEXT, hText))
            GlobalFree(hText);
    }
    CloseClipboard();
}

std::filesystem::path ExportNow(std::string_view aPlayer) noexcept
{
    const auto cMark = Coop() / "last_export";
    std::error_code error;
    // The first export: the last week.
    std::optional<std::filesystem::file_time_type> since = std::filesystem::file_time_type::clock::now() - std::chrono::days(7);
    if (const auto cTime = std::filesystem::last_write_time(cMark, error); !error)
        since = cTime;
    const auto cFiles = ExportFiles(Sources(), since);
    const std::string cId = NewReportId();
    const auto cZip = Coop() / std::format("SkyrimCoop-{}-{}.zip", SafeName(aPlayer), cId);
    const std::string cInfo = std::format("build: {}\nplayer: {}\nexported: {}\nfiles: {}\n\nplugins.txt:\n{}", BuildCommit(), aPlayer, cId,
                                          cFiles.size(), ReadText(KnownFolder(FOLDERID_LocalAppData) / "Skyrim Special Edition" / "plugins.txt"));
    if (!WriteExport(cZip, cFiles, cInfo))
    {
        spdlog::error("Export: could not write {}", cZip.string());
        return {};
    }
    std::ofstream(cMark, std::ios::trunc) << cId;
    spdlog::info("Export: {} files into {}", cFiles.size(), cZip.string());
    return cZip;
}
} // namespace Diagnostics::Folders
