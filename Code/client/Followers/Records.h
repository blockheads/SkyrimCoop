#pragma once

#include <Followers/Anchors.h>
#include <Wire/Serialization.h>
#include <functional>

namespace Followers
{
inline std::string Encode(const Anchors& acAnchors) noexcept
{
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    Wire::Serialization::WriteVarInt(writer, acAnchors.All().size());
    for (const auto& [cReference, cKey] : acAnchors.All())
    {
        Wire::Serialization::WriteVarInt(writer, cReference);
        Wire::Serialization::WriteString(writer, cKey);
    }
    return {reinterpret_cast<const char*>(buffer.GetData()), writer.Size()};
}

// References are remapped by SKSE on load; member keys are independent of their current actors.
inline bool Decode(Anchors& aAnchors, std::string_view aData, uint32_t aVersion,
                   const std::function<bool(uint32_t, uint32_t&)>& acResolve = {}) noexcept
{
    aAnchors.Clear();
    if (aVersion != 1 || aData.empty() || aData.size() > 65536)
        return false;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(aData.data()), aData.size()));
    const auto cCount = Wire::Serialization::ReadVarInt(reader);
    if (cCount > 64)
        return false;
    Anchors decoded;
    for (uint64_t i = 0; i < cCount; ++i)
    {
        const auto cReference = Wire::Serialization::ReadVarInt(reader);
        const auto cLength = Wire::Serialization::ReadVarInt(reader);
        const auto cStart = reader.GetBytePosition();
        if (!cReference || cReference > UINT32_MAX || !cLength || cLength > 1024 || cStart + cLength > aData.size())
            return false;
        std::string key(cLength, '\0');
        reader.ReadBytes(std::span(reinterpret_cast<uint8_t*>(key.data()), key.size()));
        uint32_t reference = static_cast<uint32_t>(cReference);
        if (!acResolve || acResolve(reference, reference))
            decoded.Follow(reference, std::move(key));
    }
    if (reader.GetBytePosition() != aData.size())
        return false;
    aAnchors = std::move(decoded);
    return true;
}
} // namespace Followers
