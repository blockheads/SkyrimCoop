#pragma once

#include <Structs/Movement.h>
#include <Structs/ActionEvent.h>
#include <Wire/Buffer.h>

using Wire::Buffer;

struct ReferenceUpdate
{
    // Generous: the actions one actor queues between two sends, and the actors one game moves.
    static constexpr uint64_t kMaxActionEvents = 1024;
    static constexpr uint64_t kMaxPerMessage = 4096;

    ReferenceUpdate() = default;
    ~ReferenceUpdate() = default;

    bool operator==(const ReferenceUpdate& acRhs) const noexcept;
    bool operator!=(const ReferenceUpdate& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader);

    Movement UpdatedMovement{};
    std::vector<ActionEvent> ActionEvents{};
};
