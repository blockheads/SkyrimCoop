#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>

// Where bug reports go and what they are called (docs/design/bug-reports.md). No engine.
namespace Diagnostics
{
// "2026-10-29-2214-a3f": the local time to the minute and 12 random bits.
[[nodiscard]] std::string ReportId(const std::tm& acLocal, uint32_t aRandom);
// A new id from the local time now.
[[nodiscard]] std::string NewReportId();
// File-safe: letters, digits, '-' and '_' kept, anything else '_', at most 48 characters; aEmpty for nothing left.
[[nodiscard]] std::string SafeName(std::string_view aText, std::string_view aEmpty = "player");
// A save kept for reports, once per save written: its name and its write time ("AutoSave1-1d9f3a2b").
[[nodiscard]] std::string StoredSaveName(std::string_view aStem, int64_t aWriteTime);
// <root>/<id>/<host|guest>-<player>: one game's part of a report.
[[nodiscard]] std::filesystem::path PartFolder(const std::filesystem::path& acRoot, std::string_view aId, bool aHost,
                                               std::string_view aPlayer);
} // namespace Diagnostics
