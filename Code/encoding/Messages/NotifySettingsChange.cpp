#include <Messages/NotifySettingsChange.h>
#include <Wire/Serialization.h>

void NotifySettingsChange::SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept
{
    Settings.Serialize(aWriter);
}

void NotifySettingsChange::DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept
{
    NotifyMessage::DeserializeRaw(aReader);

    Settings.Deserialize(aReader);
}
