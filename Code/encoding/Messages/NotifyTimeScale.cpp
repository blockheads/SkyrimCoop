#include <Messages/NotifyTimeScale.h>

#include <TiltedCore/Serialization.hpp>

void NotifyTimeScale::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteFloat(aWriter, Multiplier);
}

void NotifyTimeScale::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Multiplier = TiltedPhoques::Serialization::ReadFloat(aReader);
}
