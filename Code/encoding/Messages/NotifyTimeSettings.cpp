#include <Messages/NotifyTimeSettings.h>

void NotifyTimeSettings::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    timeModel.Serialize(aWriter);
}

void NotifyTimeSettings::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    timeModel.Deserialize(aReader);
}
