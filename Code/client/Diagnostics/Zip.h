#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace Diagnostics
{
// A zip archive written as files are added (deflate, zlib; no ZIP64: under 4 GB). No engine.
struct ZipWriter
{
    explicit ZipWriter(const std::filesystem::path& acPath) noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_out.is_open(); }
    // aName: the path in the archive, '/' separated.
    bool AddBytes(std::string_view aName, std::string_view aData) noexcept;
    bool AddFile(std::string_view aName, const std::filesystem::path& acFile) noexcept;
    // Writes the central directory; false if anything failed.
    bool Finish() noexcept;

private:
    struct Entry
    {
        std::string Name;
        uint32_t Crc, Compressed, Size, Offset;
    };

    std::ofstream m_out;
    std::vector<Entry> m_entries;
    bool m_failed = false;
};
} // namespace Diagnostics
