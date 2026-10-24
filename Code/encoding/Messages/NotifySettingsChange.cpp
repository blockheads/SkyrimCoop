#include <Messages/NotifySettingsChange.h>
#include <Wire/Serialization.h>

void NotifySettingsChange::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Difficulty);
}

void NotifySettingsChange::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Difficulty = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
