#include <Messages/NotifyStealthEye.h>

#include <Wire/Serialization.h>

#include <algorithm>

using Wire::Serialization;

void NotifyStealthEye::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, static_cast<uint64_t>(std::max(Level, kUnseen) - kUnseen));
    Serialization::WriteVarInt(aWriter, Seeing);
    Serialization::WriteFloat(aWriter, Stealth);
}

void NotifyStealthEye::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Level = static_cast<int32_t>(Serialization::ReadVarInt(aReader) & 0x7FFFFFFF) + kUnseen;
    Seeing = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Stealth = Serialization::ReadFloat(aReader);
}
