#pragma once

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct RequestEquipmentChanges;

struct MagicEquipment
{
    bool operator==(const MagicEquipment& acRhs) const noexcept;
    bool operator!=(const MagicEquipment& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    GameId LeftHandSpell{};
    GameId RightHandSpell{};
    GameId Shout{};
};
