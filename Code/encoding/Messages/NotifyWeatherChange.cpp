#include <Messages/NotifyWeatherChange.h>

void NotifyWeatherChange::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
}

void NotifyWeatherChange::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Id.Deserialize(aReader);
}
