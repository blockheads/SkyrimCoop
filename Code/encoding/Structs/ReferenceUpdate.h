#pragma once

#include <Structs/Movement.h>
#include <Structs/ActionEvent.h>
#include <Wire/Buffer.h>

using Wire::Buffer;

struct ReferenceUpdate
{
    ReferenceUpdate() = default;
    ~ReferenceUpdate() = default;

    bool operator==(const ReferenceUpdate& acRhs) const noexcept;
    bool operator!=(const ReferenceUpdate& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader);

    Movement UpdatedMovement{};
    std::vector<ActionEvent> ActionEvents{};
};
