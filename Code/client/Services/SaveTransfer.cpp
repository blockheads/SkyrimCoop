#include <Services/SaveTransfer.h>

#include <snappy.h>

#include <cstring>

namespace SaveTransfer
{
// The game file's size (4 bytes), the game file, the co-save; then compressed whole.
std::string Pack(const SaveFiles& acFiles) noexcept
{
    std::string raw(4, '\0');
    const auto cSize = static_cast<uint32_t>(acFiles.Game.size());
    std::memcpy(raw.data(), &cSize, sizeof(cSize));
    raw += acFiles.Game;
    raw += acFiles.CoSave;
    std::string packed;
    snappy::Compress(raw.data(), raw.size(), &packed);
    return packed;
}

std::optional<SaveFiles> Unpack(std::string_view acPacked) noexcept
{
    std::string raw;
    if (!snappy::Uncompress(acPacked.data(), acPacked.size(), &raw) || raw.size() < 4)
        return std::nullopt;
    uint32_t gameSize = 0;
    std::memcpy(&gameSize, raw.data(), sizeof(gameSize));
    if (gameSize > raw.size() - 4)
        return std::nullopt;
    return SaveFiles{raw.substr(4, gameSize), raw.substr(4 + gameSize)};
}

std::vector<NotifySaveChunk> Split(const std::string& acPacked) noexcept
{
    std::vector<NotifySaveChunk> chunks;
    for (size_t offset = 0; offset < acPacked.size(); offset += NotifySaveChunk::kMaxData)
    {
        NotifySaveChunk& chunk = chunks.emplace_back();
        chunk.Offset = static_cast<uint32_t>(offset);
        chunk.Total = static_cast<uint32_t>(acPacked.size());
        chunk.Data = acPacked.substr(offset, NotifySaveChunk::kMaxData);
    }
    return chunks;
}

bool Assembly::Add(const NotifySaveChunk& acChunk) noexcept
{
    if (acChunk.Offset == 0)
    {
        m_data.clear();
        m_total = acChunk.Total;
    }
    if (acChunk.Total != m_total || acChunk.Offset != m_data.size() || m_data.size() + acChunk.Data.size() > m_total)
        return false;
    m_data += acChunk.Data;
    return true;
}
} // namespace SaveTransfer
