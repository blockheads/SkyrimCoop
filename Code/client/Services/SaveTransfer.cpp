#include <Services/SaveTransfer.h>
#include "../../external/picosha2/picosha2.h"
#include <snappy.h>
#include <array>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#endif

namespace SaveTransfer
{
namespace
{
constexpr auto cGear = [] {
    std::array<uint64_t, 256> table{};
    for (uint64_t i = 0; i < table.size(); ++i)
    {
        uint64_t value = i + 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        table[i] = value ^ (value >> 31);
    }
    return table;
}();

// Content boundaries resynchronize after inserted bytes; block sizes bound both memory and wire messages.
template<class T> void Blocks(std::string_view acData, T&& aVisit)
{
    size_t start = 0;
    uint64_t rolling = 0;
    for (size_t i = 0; i < acData.size(); ++i)
    {
        rolling = (rolling << 1) + cGear[static_cast<uint8_t>(acData[i])];
        const size_t cSize = i + 1 - start;
        if (cSize == kMaxBlock || (cSize >= kMinBlock && !(rolling & 0x1fff)) || i + 1 == acData.size())
        {
            aVisit(start, acData.substr(start, cSize));
            start = i + 1;
            rolling = 0;
        }
    }
}
}

std::string Pack(const SaveFiles& acFiles) noexcept
{
    if (acFiles.Game.size() > kMaxSave - 4 || acFiles.CoSave.size() > kMaxSave - 4 - acFiles.Game.size())
        return {};
    std::string packed(4, '\0');
    const auto cSize = static_cast<uint32_t>(acFiles.Game.size());
    std::memcpy(packed.data(), &cSize, sizeof(cSize));
    packed += acFiles.Game;
    packed += acFiles.CoSave;
    return packed;
}

std::optional<SaveFiles> Unpack(std::string_view acPacked) noexcept
{
    if (acPacked.size() < 4 || acPacked.size() > kMaxSave)
        return std::nullopt;
    uint32_t gameSize = 0;
    std::memcpy(&gameSize, acPacked.data(), sizeof(gameSize));
    if (gameSize > acPacked.size() - 4)
        return std::nullopt;
    return SaveFiles{std::string(acPacked.substr(4, gameSize)), std::string(acPacked.substr(4 + gameSize))};
}

std::string Digest(std::string_view acBytes) noexcept
{
    std::string digest(picosha2::k_digest_size, '\0');
#if defined(_WIN32)
    // CNG avoids the portable implementation's iterator overhead in development builds.
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                   reinterpret_cast<PUCHAR>(const_cast<char*>(acBytes.data())), static_cast<ULONG>(acBytes.size()),
                   reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size())) >= 0)
        return digest;
#endif
    picosha2::hash256(acBytes.begin(), acBytes.end(), digest.begin(), digest.end());
    return digest;
}

std::vector<NotifySaveChunk> Split(const std::string& acPacked) noexcept
{
    std::vector<NotifySaveChunk> chunks;
    if (acPacked.empty() || acPacked.size() > kMaxSave)
        return chunks;
    Blocks(acPacked, [&](size_t aOffset, std::string_view acBlock) {
        auto& chunk = chunks.emplace_back();
        chunk.Offset = static_cast<uint32_t>(aOffset);
        chunk.Total = static_cast<uint32_t>(acPacked.size());
        chunk.RawSize = static_cast<uint32_t>(acBlock.size());
        chunk.Hash = Digest(acBlock);
        snappy::Compress(acBlock.data(), acBlock.size(), &chunk.Data);
    });
    return chunks;
}

void Cache::Load(const SaveFiles& acFiles) noexcept
{
    Clear();
    m_data = Pack(acFiles);
    Blocks(m_data, [&](size_t aOffset, std::string_view acBlock) {
        m_blocks.try_emplace(Digest(acBlock), aOffset, acBlock.size());
    });
}

void Cache::Clear() noexcept
{
    m_blocks.clear();
    std::string{}.swap(m_data);
}

std::vector<std::string> Cache::Hashes() const
{
    std::vector<std::string> hashes;
    hashes.reserve(m_blocks.size());
    for (const auto& [hash, range] : m_blocks)
        hashes.push_back(hash);
    return hashes;
}

std::optional<std::string_view> Cache::Find(const std::string& acDigest) const noexcept
{
    const auto it = m_blocks.find(acDigest);
    if (it == m_blocks.end())
        return std::nullopt;
    const auto [cOffset, cSize] = it->second;
    return std::string_view(m_data).substr(cOffset, cSize);
}

bool Assembly::Add(const NotifySaveChunk& acChunk, const Cache* acpCache) noexcept
{
    if (!acChunk.Total || acChunk.Total > kMaxSave || !acChunk.RawSize || acChunk.RawSize > kMaxBlock ||
        acChunk.Hash.size() != 32 || acChunk.Data.size() > NotifySaveChunk::kMaxData)
        return false;
    if (acChunk.Offset == 0)
    {
        m_data.clear();
        m_total = acChunk.Total;
    }
    if (acChunk.Total != m_total || acChunk.Offset != m_data.size() || acChunk.RawSize > m_total - m_data.size())
        return false;
    std::string raw;
    if (acChunk.Data.empty())
    {
        const auto cCached = acpCache ? acpCache->Find(acChunk.Hash) : std::nullopt;
        if (!cCached)
            return false;
        raw = *cCached;
    }
    else
    {
        size_t length = 0;
        if (!snappy::GetUncompressedLength(acChunk.Data.data(), acChunk.Data.size(), &length) || length != acChunk.RawSize ||
            !snappy::Uncompress(acChunk.Data.data(), acChunk.Data.size(), &raw))
            return false;
    }
    if (raw.size() != acChunk.RawSize || Digest(raw) != acChunk.Hash)
        return false;
    m_data += raw;
    return true;
}
} // namespace SaveTransfer
