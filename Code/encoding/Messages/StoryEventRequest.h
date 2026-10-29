#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// A player's story event the host's story manager judges for that player's character (party-model.md rule 4).
struct StoryEvent
{
    enum Kind : uint8_t
    {
        kLocationChange, // CLOC: OldLocation to Location
        kCraft,          // CRFT: Created at Workbench, in Location
    };

    bool operator==(const StoryEvent& acRhs) const noexcept = default;

    Kind Type = kLocationChange;
    uint32_t Character = 0; // the session id of the character it is judged for (the host's rule fills it)
    GameId Workbench;
    GameId OldLocation;
    GameId Location;
    GameId Created;
};

// A guest's story event its own game cannot judge (its story manager starts no quest): raised in the host's game.
struct StoryEventRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kStoryEventRequest;

    StoryEventRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const StoryEventRequest& acRhs) const noexcept { return Event == acRhs.Event && GetOpcode() == acRhs.GetOpcode(); }

    StoryEvent Event;
};
