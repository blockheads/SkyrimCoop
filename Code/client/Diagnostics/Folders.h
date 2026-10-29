#pragma once

#include "CrashReports.h"
#include "Export.h"

#include <filesystem>
#include <string>
#include <string_view>

// Where this game keeps what it reports, and the zip a player sends (docs/design/bug-reports.md).
namespace Diagnostics::Folders
{
// Documents/My Games/Skyrim Special Edition/SkyrimCoop
std::filesystem::path Coop() noexcept;
std::filesystem::path Reports() noexcept;
ExportSources Sources() noexcept;
CrashReports::Marks RunMarks() noexcept;
// The path as the player's desktop shows it: under Wine/Proton the Linux path, else as it is.
std::string ForPlayer(const std::filesystem::path& acPath) noexcept;
// Replaces the clipboard's text (Wine passes it to the desktop's).
void CopyToClipboard(const std::string& acText) noexcept;
// Everything since the last export into Coop()/SkyrimCoop-<player>-<date>.zip; that zip, empty if it failed.
std::filesystem::path ExportNow(std::string_view aPlayer) noexcept;
} // namespace Diagnostics::Folders
