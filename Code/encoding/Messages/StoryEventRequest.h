#pragma once

#include "Message.h"

#include <Structs/GameId.h>

#include <vector>

// A player's story event the host's story manager judges for that player's character (party-model.md rule 4): any
// type the engine registers (CLOC, CRFT, SKIL, a mod's SCPT...), its members as the type lays them out
// (docs/re/systems/story-manager.md, "A guest's story events").
struct StoryEvent
{
    struct Member
    {
        enum Kind : uint8_t
        {
            kNone,      // a null member, or one the session cannot name (a created reference)
            kCharacter, // the player whose event it is: its character on the host
            kReference, // a world reference (a workbench, a container)
            kForm,      // a form, location or keyword
            kInteger,
        };

        bool operator==(const Member&) const noexcept = default;
        template <class TArchive> void Fields(TArchive& aArchive) noexcept
        {
            aArchive(Wire::Bits<8>(Type));
            if (Type == kReference || Type == kForm)
                aArchive(Form);
            else if (Type == kInteger)
                aArchive(Value);
        }

        Kind Type = kNone;
        GameId Form{};      // kReference, kForm
        uint32_t Value = 0; // kInteger
    };
    // A type has at most this many members (BGSStoryEvent's).
    static constexpr size_t kMaxMembers = 6;
    // A type's id from its four characters, as the engine holds them: TypeOf("CLOC").
    static constexpr uint32_t TypeOf(const char (&acId)[5]) noexcept
    {
        return static_cast<uint8_t>(acId[0]) | static_cast<uint8_t>(acId[1]) << 8 | static_cast<uint8_t>(acId[2]) << 16 |
               static_cast<uint32_t>(static_cast<uint8_t>(acId[3])) << 24;
    }

    bool operator==(const StoryEvent&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Wire::Bits<32>(Type), Wire::List(Members, kMaxMembers)); }

    uint32_t Type = 0;      // its registered type's id, four characters (BGSRegisteredStoryEvent::uniqueID): 'CLOC'
    uint32_t Character = 0; // the session id of the character it is judged for (the host's rule fills it)
    std::vector<Member> Members;
};

// A guest's story event its own game cannot judge (its story manager starts no quest): raised in the host's game.
struct StoryEventRequest final : MessageOf<StoryEventRequest, kStoryEventRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Event); }
    bool operator==(const StoryEventRequest&) const noexcept = default;

    StoryEvent Event;
};
