#include <Structs/ReferenceUpdate.h>
#include <Wire/Serialization.h>

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

    ActionEvents.resize(Serialization::ReadCount(aReader, kMaxActionEvents));

    for (auto& entry : ActionEvents)
        entry.ApplyDifferential(aReader);
}
