#include "Zip.h"

#include <zlib.h>

namespace Diagnostics
{
namespace
{
void Put(std::string& aOut, uint32_t aValue, int aBytes)
{
    for (int i = 0; i < aBytes; ++i)
        aOut += static_cast<char>((aValue >> (8 * i)) & 0xFF);
}

// Raw deflate (no zlib header), as zip stores it.
bool Deflate(std::string_view aData, std::string& aOut) noexcept
{
    z_stream stream{};
    if (deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    aOut.resize(deflateBound(&stream, static_cast<uLong>(aData.size())));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(aData.data()));
    stream.avail_in = static_cast<uInt>(aData.size());
    stream.next_out = reinterpret_cast<Bytef*>(aOut.data());
    stream.avail_out = static_cast<uInt>(aOut.size());
    const int cResult = deflate(&stream, Z_FINISH);
    aOut.resize(stream.total_out);
    deflateEnd(&stream);
    return cResult == Z_STREAM_END;
}
} // namespace

ZipWriter::ZipWriter(const std::filesystem::path& acPath) noexcept
    : m_out(acPath, std::ios::binary)
{
}

bool ZipWriter::AddBytes(std::string_view aName, std::string_view aData) noexcept
{
    std::string compressed;
    if (!m_out || aData.size() > 0xFFFFFFF0u || !Deflate(aData, compressed))
    {
        m_failed = true;
        return false;
    }
    const auto cOffset = static_cast<uint32_t>(m_out.tellp());
    const auto cCrc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(aData.data()), static_cast<uInt>(aData.size())));
    std::string header;
    Put(header, 0x04034b50, 4); // local file header
    Put(header, 20, 2);         // version needed
    Put(header, 0x0800, 2);     // UTF-8 names
    Put(header, 8, 2);          // deflate
    Put(header, 0, 4);          // time, date
    Put(header, cCrc, 4);
    Put(header, static_cast<uint32_t>(compressed.size()), 4);
    Put(header, static_cast<uint32_t>(aData.size()), 4);
    Put(header, static_cast<uint32_t>(aName.size()), 2);
    Put(header, 0, 2);
    header += aName;
    m_out.write(header.data(), static_cast<std::streamsize>(header.size()));
    m_out.write(compressed.data(), static_cast<std::streamsize>(compressed.size()));
    m_entries.push_back({std::string(aName), cCrc, static_cast<uint32_t>(compressed.size()), static_cast<uint32_t>(aData.size()), cOffset});
    return static_cast<bool>(m_out);
}

bool ZipWriter::AddFile(std::string_view aName, const std::filesystem::path& acFile) noexcept
{
    std::ifstream in(acFile, std::ios::binary);
    if (!in)
        return false;
    const std::string cData((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return AddBytes(aName, cData);
}

bool ZipWriter::Finish() noexcept
{
    const auto cStart = static_cast<uint32_t>(m_out.tellp());
    std::string directory;
    for (const auto& cEntry : m_entries)
    {
        Put(directory, 0x02014b50, 4); // central directory header
        Put(directory, 20, 2);         // made by
        Put(directory, 20, 2);
        Put(directory, 0x0800, 2);
        Put(directory, 8, 2);
        Put(directory, 0, 4);
        Put(directory, cEntry.Crc, 4);
        Put(directory, cEntry.Compressed, 4);
        Put(directory, cEntry.Size, 4);
        Put(directory, static_cast<uint32_t>(cEntry.Name.size()), 2);
        Put(directory, 0, 2); // extra
        Put(directory, 0, 2); // comment
        Put(directory, 0, 2); // disk
        Put(directory, 0, 2); // internal attributes
        Put(directory, 0, 4); // external attributes
        Put(directory, cEntry.Offset, 4);
        directory += cEntry.Name;
    }
    const auto cSize = static_cast<uint32_t>(directory.size());
    Put(directory, 0x06054b50, 4); // end of central directory
    Put(directory, 0, 2);
    Put(directory, 0, 2);
    Put(directory, static_cast<uint32_t>(m_entries.size()), 2);
    Put(directory, static_cast<uint32_t>(m_entries.size()), 2);
    Put(directory, cSize, 4);
    Put(directory, cStart, 4);
    Put(directory, 0, 2);
    m_out.write(directory.data(), static_cast<std::streamsize>(directory.size()));
    m_out.close();
    return !m_failed && !m_out.fail();
}
} // namespace Diagnostics
