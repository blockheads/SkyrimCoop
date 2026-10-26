#include <Services/CharacterRecords.h>

#include <Wire/Serialization.h>

#include <spdlog/spdlog.h>

using Wire::Serialization;

void CharacterRecords::Put(PlayerRecord aRecord) noexcept
{
    if (aRecord.Key.empty())
        return;
    std::string key = aRecord.Key;
    m_records.insert_or_assign(std::move(key), std::move(aRecord));
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
std::string CharacterRecords::Encode() const noexcept
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Serialization::WriteVarInt(writer, m_records.size());
    for (const auto& [cKey, cRecord] : m_records)
    {
        Wire::Buffer one;
        Wire::Buffer::Writer recordWriter(&one);
        cRecord.Serialize(recordWriter);
        Serialization::WriteVarInt(writer, recordWriter.Size());
        writer.WriteBytes(std::span(one.GetData(), recordWriter.Size()));
    }
    return {reinterpret_cast<const char*>(buffer.GetData()), writer.Size()};
}

bool CharacterRecords::Decode(std::string_view aData, uint32_t aVersion) noexcept
{
    m_records.clear();
    if (aVersion != kVersion)
    {
        spdlog::warn("CharacterRecords: version {} in the co-save, this build reads {}", aVersion, kVersion);
        return false;
    }
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(aData.data()), aData.size()));
    const uint64_t cCount = Serialization::ReadVarInt(reader);
    for (uint64_t i = 0; i < cCount; ++i)
    {
        const uint64_t cSize = Serialization::ReadVarInt(reader);
        const size_t cStart = reader.GetBytePosition();
        if (reader.GetBitPosition() % 8 != 0 || cStart + cSize > aData.size())
        {
            spdlog::warn("CharacterRecords: record {} of {} runs past the co-save's {} bytes", i + 1, cCount, aData.size());
            m_records.clear();
            return false;
        }
        Wire::Buffer::Reader recordReader(std::span(reinterpret_cast<const uint8_t*>(aData.data()) + cStart, cSize));
        PlayerRecord record;
        record.Deserialize(recordReader);
        Put(std::move(record));
        std::vector<uint8_t> skipped(cSize);
        reader.ReadBytes(skipped);
    }
    return true;
}
