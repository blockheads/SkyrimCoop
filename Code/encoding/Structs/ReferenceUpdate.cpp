#include <Structs/ReferenceUpdate.h>
#include <Wire/Serialization.h>
#include <stdexcept>

using Wire::Serialization;

bool ReferenceUpdate::operator==(const ReferenceUpdate& acRhs) const noexcept
{
    return UpdatedMovement == acRhs.UpdatedMovement && ActionEvents == acRhs.ActionEvents;
}

bool ReferenceUpdate::operator!=(const ReferenceUpdate& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ReferenceUpdate::Serialize(Wire::Buffer::Writer& aWriter) const noexcept
{
    UpdatedMovement.Serialize(aWriter);

    Serialization::WriteVarInt(aWriter, ActionEvents.size());

    for (auto& entry : ActionEvents)
    {
        entry.GenerateDifferential(ActionEvent{}, aWriter);
    }
}

void ReferenceUpdate::Deserialize(Wire::Buffer::Reader& aReader)
{
    UpdatedMovement.Deserialize(aReader);

    const auto count = Serialization::ReadVarInt(aReader);

    ActionEvents.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        ActionEvents[i].ApplyDifferential(aReader);
    }
}
