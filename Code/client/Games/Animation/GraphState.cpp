#include "GraphState.h"

#include <Wire/Buffer.h>
#include <Wire/Serialization.h>
#include <algorithm>
#include <bit>
#include <cstring>

namespace GraphState
{
namespace
{
constexpr uint64_t cMaxNodes = 16384;
constexpr uint64_t cMaxBytes = 1u << 20;

void WriteBytes(Wire::Buffer::Writer& aWriter, std::string_view aBytes)
{
    Wire::Serialization::WriteVarInt(aWriter, aBytes.size());
    aWriter.WriteBytes({reinterpret_cast<const uint8_t*>(aBytes.data()), aBytes.size()});
}

bool ReadBytes(Wire::Buffer::Reader& aReader, std::string& aBytes)
{
    uint64_t size = 0;
    if (!Wire::Serialization::TryReadVarInt(aReader, size) || size > cMaxBytes)
        return false;
    aBytes.resize(size);
    return aReader.ReadBytes({reinterpret_cast<uint8_t*>(aBytes.data()), aBytes.size()});
}

void WriteFloat(Wire::Buffer::Writer& aWriter, float aValue)
{
    aWriter.WriteBits(std::bit_cast<uint32_t>(aValue), 32);
}

bool ReadFloat(Wire::Buffer::Reader& aReader, float& aValue)
{
    uint64_t bits = 0;
    if (!aReader.ReadBits(bits, 32))
        return false;
    aValue = std::bit_cast<float>(uint32_t(bits));
    return true;
}

// Its used sync points only (most nodes have none), then its times and flags.
void WriteSync(Wire::Buffer::Writer& aWriter, const SyncInfo& acSync)
{
    const uint32_t cPoints = uint32_t(std::clamp<int>(acSync.NumSyncPoints, 0, 8));
    aWriter.WriteBits(cPoints, 4);
    for (uint32_t i = 0; i < cPoints; ++i)
    {
        aWriter.WriteBits(uint32_t(acSync.Points[i].Id), 32);
        WriteFloat(aWriter, acSync.Points[i].Time);
    }
    WriteFloat(aWriter, acSync.BaseFrequency);
    WriteFloat(aWriter, acSync.LocalTime);
    WriteFloat(aWriter, acSync.PlaybackSpeed);
    aWriter.WriteBits(uint8_t(acSync.NumSyncPoints), 8);
    aWriter.WriteBits((acSync.IsCyclic ? 1 : 0) | (acSync.IsMirrored ? 2 : 0) | (acSync.IsAdditive ? 4 : 0), 3);
}

bool ReadSync(Wire::Buffer::Reader& aReader, SyncInfo& aSync)
{
    uint64_t points = 0, numSyncPoints = 0, flags = 0;
    if (!aReader.ReadBits(points, 4) || points > 8)
        return false;
    for (uint32_t i = 0; i < points; ++i)
    {
        uint64_t id = 0;
        if (!aReader.ReadBits(id, 32) || !ReadFloat(aReader, aSync.Points[i].Time))
            return false;
        aSync.Points[i].Id = int32_t(uint32_t(id));
    }
    if (!ReadFloat(aReader, aSync.BaseFrequency) || !ReadFloat(aReader, aSync.LocalTime) || !ReadFloat(aReader, aSync.PlaybackSpeed) ||
        !aReader.ReadBits(numSyncPoints, 8) || !aReader.ReadBits(flags, 3))
        return false;
    aSync.NumSyncPoints = int8_t(uint8_t(numSyncPoints));
    aSync.IsCyclic = flags & 1;
    aSync.IsMirrored = flags & 2;
    aSync.IsAdditive = flags & 4;
    return true;
}
} // namespace

uint32_t Check(std::string_view aClass, std::string_view aName) noexcept
{
    uint32_t hash = 2166136261u; // FNV-1a
    const auto cMix = [&](std::string_view aText) {
        for (const char c : aText)
            hash = (hash ^ uint8_t(c)) * 16777619u;
    };
    cMix(aClass);
    cMix("/");
    cMix(aName);
    return hash;
}

std::string Encode(const std::vector<Node>& acNodes)
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Wire::Serialization::WriteVarInt(writer, acNodes.size());
    for (const auto& cNode : acNodes)
    {
        Wire::Serialization::WriteVarInt(writer, cNode.Id);
        writer.WriteBits(cNode.Check, 32);
        WriteSync(writer, cNode.Sync);
        writer.WriteBits(cNode.Activated ? 1 : 0, 1);
        writer.WriteBits(cNode.State ? 1 : 0, 1);
        if (cNode.State)
            WriteBytes(writer, *cNode.State);
        WriteBytes(writer, cNode.Runtime);
    }
    return std::string(reinterpret_cast<const char*>(buffer.GetData()), writer.Size());
}

bool Decode(std::string_view aBytes, std::vector<Node>& aNodes)
{
    aNodes.clear();
    Wire::Buffer::Reader reader{std::span(reinterpret_cast<const uint8_t*>(aBytes.data()), aBytes.size())};
    uint64_t count = 0;
    if (!Wire::Serialization::TryReadVarInt(reader, count) || count > cMaxNodes)
        return false;
    aNodes.resize(count);
    for (auto& node : aNodes)
    {
        uint64_t id = 0, check = 0, activated = 0, hasState = 0;
        if (!Wire::Serialization::TryReadVarInt(reader, id) || id > UINT16_MAX || !reader.ReadBits(check, 32) || !ReadSync(reader, node.Sync) ||
            !reader.ReadBits(activated, 1) || !reader.ReadBits(hasState, 1))
            return false;
        node.Id = uint16_t(id);
        node.Check = uint32_t(check);
        node.Activated = activated != 0;
        if (hasState && !ReadBytes(reader, node.State.emplace()))
            return false;
        if (!ReadBytes(reader, node.Runtime))
            return false;
    }
    return true;
}
} // namespace GraphState
