#include <Messages/NotifyTimeScale.h>

#include <Wire/Serialization.h>

void NotifyTimeScale::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Wire::Serialization::WriteFloat(aWriter, Multiplier);
}

void NotifyTimeScale::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Multiplier = Wire::Serialization::ReadFloat(aReader);
}
