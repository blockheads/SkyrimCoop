#include <Structs/GameVersions.h>
#include <Wire/Serialization.h>

using Wire::Serialization;

void GameVersions::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Runtime);
    Serialization::WriteVarInt(aWriter, Skse);
}

void GameVersions::Deserialize(Wire::Buffer::Reader& aReader) noexcept
{
    Runtime = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Skse = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

std::string GameVersions::Format(uint32_t aPacked) noexcept
{
    std::string text = std::to_string(aPacked >> 24) + "." + std::to_string((aPacked >> 16) & 0xFF) + "." + std::to_string((aPacked >> 4) & 0xFFF);
    if (aPacked & 0xF)
        text += "." + std::to_string(aPacked & 0xF);
    return text;
}
