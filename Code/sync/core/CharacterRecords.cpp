#include <core/CharacterRecords.h>

#include <Wire/Serialization.h>

#include <spdlog/spdlog.h>
#include <limits>

using Wire::Serialization;

void CharacterRecords::SetStorySpell(const GameId& acSpell, bool aAdded) noexcept
{
    m_storySpells[acSpell] = aAdded;
}

void CharacterRecords::ApplyStorySpells(PlayerRecord& aRecord) const noexcept
{
    for (const auto& [cSpell, cAdded] : m_storySpells)
    {
        std::erase(aRecord.Spells, cSpell);
        if (cAdded)
            aRecord.Spells.push_back(cSpell);
    }
}

void CharacterRecords::Put(PlayerRecord aRecord) noexcept
{
    if (aRecord.Key.empty())
        return;
    if (const auto it = m_records.find(aRecord.Key); it != m_records.end())
    {
        aRecord.WorldSpace = it->second.WorldSpace;
        aRecord.Cell = it->second.Cell;
        aRecord.Position = it->second.Position;
    }
    if (const auto it = m_received.find(aRecord.Key); it != m_received.end())
        m_received.erase(it);
    std::string key = aRecord.Key;
    m_records.insert_or_assign(std::move(key), std::move(aRecord));
}

void CharacterRecords::Put(PlayerRecord aRecord, Clock::time_point aReceived) noexcept
{
    std::string key = aRecord.Key;
    Put(std::move(aRecord));
    if (!key.empty())
        m_received.insert_or_assign(std::move(key), aReceived);
}

namespace
{
float SecondsBetween(CharacterRecords::Clock::time_point aFrom, CharacterRecords::Clock::time_point aTo) noexcept
{
    return aTo > aFrom ? std::chrono::duration<float>(aTo - aFrom).count() : 0.f;
}
} // namespace

void CharacterRecords::Settle(std::string_view aKey, Clock::time_point aNow) noexcept
{
    const auto cReceived = m_received.find(aKey);
    if (cReceived == m_received.end())
        return;
    if (const auto it = m_records.find(aKey); it != m_records.end())
        it->second.Own.Age(SecondsBetween(cReceived->second, aNow));
    m_received.erase(cReceived);
}

const PlayerRecord* CharacterRecords::Find(std::string_view aKey) const noexcept
{
    const auto it = m_records.find(aKey);
    return it == m_records.end() ? nullptr : &it->second;
}

void CharacterRecords::Remove(std::string_view aKey) noexcept
{
    if (const auto it = m_records.find(aKey); it != m_records.end())
        m_records.erase(it);
    if (const auto it = m_received.find(aKey); it != m_received.end())
        m_received.erase(it);
}

void CharacterRecords::SetPlace(std::string_view aKey, const GameId& acWorldSpace, const GameId& acCell,
                                const std::array<float, 3>& acPosition) noexcept
{
    const auto it = m_records.find(aKey);
    if (it == m_records.end())
        return;
    it->second.WorldSpace = acWorldSpace;
    it->second.Cell = acCell;
    it->second.Position = acPosition;
}

// The count, then each record with its size first: a reader skips what it cannot read.
std::string CharacterRecords::Encode(Clock::time_point aNow) const noexcept
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, m_records.size());
    for (const auto& [cKey, cRecord] : m_records)
    {
        Wire::Buffer one;
        Wire::Buffer::Writer recordWriter(&one);
        if (const auto cReceived = m_received.find(cKey); cReceived != m_received.end())
        {
            PlayerRecord aged = cRecord;
            aged.Own.Age(SecondsBetween(cReceived->second, aNow));
            aged.Serialize(recordWriter);
        }
        else
            cRecord.Serialize(recordWriter);
        Serialization::WriteVarInt(writer, recordWriter.Size());
        writer.WriteBytes(std::span(one.GetData(), recordWriter.Size()));
    }
    Serialization::WriteVarInt(writer, m_storySpells.size());
    for (const auto& [cSpell, cAdded] : m_storySpells)
    {
        cSpell.Serialize(writer);
        Serialization::WriteBool(writer, cAdded);
    }
    return {reinterpret_cast<const char*>(buffer.GetData()), writer.Size()};
}

bool CharacterRecords::Decode(std::string_view aData, uint32_t aVersion) noexcept
{
    Clear();
    if (aVersion != kVersion && aVersion != 3)
    {
        spdlog::warn("CharacterRecords: version {} in the co-save, this build reads {}", aVersion, kVersion);
        return false;
    }
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(aData.data()), aData.size()));
    uint64_t count = 0;
    if (!Serialization::TryReadVarInt(reader, count) || count > aData.size())
        return false;
    for (uint64_t i = 0; i < count; ++i)
    {
        uint64_t size = 0;
        if (!Serialization::TryReadVarInt(reader, size))
        {
            Clear();
            return false;
        }
        const size_t cStart = reader.GetBytePosition();
        if (reader.GetBitPosition() % 8 != 0 || cStart > aData.size() || size > aData.size() - cStart)
        {
            spdlog::warn("CharacterRecords: record {} of {} runs past the co-save's {} bytes", i + 1, count, aData.size());
            Clear();
            return false;
        }
        Wire::Buffer::Reader recordReader(std::span(reinterpret_cast<const uint8_t*>(aData.data()) + cStart, size));
        PlayerRecord record;
        record.Deserialize(recordReader, aVersion >= 4);
        Put(std::move(record));
        std::vector<uint8_t> skipped(size);
        reader.ReadBytes(skipped);
    }
    // Older version-3 co-saves end after the character list.
    if (reader.GetBytePosition() < aData.size())
    {
        uint64_t spells = 0;
        if (!Serialization::TryReadVarInt(reader, spells) || spells > (aData.size() * 8 - reader.GetBitPosition()) / 17)
        {
            Clear();
            return false;
        }
        for (uint64_t i = 0; i < spells; ++i)
        {
            uint64_t base = 0, mod = 0, added = 0;
            if (!Serialization::TryReadVarInt(reader, base) || base > std::numeric_limits<uint32_t>::max() ||
                !Serialization::TryReadVarInt(reader, mod) || mod > std::numeric_limits<uint32_t>::max() || !reader.ReadBits(added, 1))
            {
                Clear();
                return false;
            }
            m_storySpells[GameId(static_cast<uint32_t>(mod), static_cast<uint32_t>(base))] = added != 0;
        }
    }
    return true;
}
