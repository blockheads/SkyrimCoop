#pragma once

#include <Games/Primitives.h>
#include <Messages/StoryEventRequest.h>
#include <Misc/BSString.h>

#include <cstdint>
#include <optional>

struct Actor;
struct OwnStoryEvent;

// A queued story event (CommonLibSSE-NG BGSStoryEvent), built from its raw event by 32330: each member a reference
// handle or a number in its low 32 bits, or a form.
struct BGSStoryEvent
{
    uint32_t id;         // 00, AddEvent_Impl's (32359)
    uint32_t index;      // 04, its type's registered index
    uint64_t members[6]; // 08
};
static_assert(sizeof(BGSStoryEvent) == 0x38);

// A registered event type's member (CommonLibSSE-NG BGSStoryEventMember).
struct BGSStoryEventMember
{
    uint32_t type;     // 00, StoryEventRules::MemberType
    uint32_t uniqueID; // 04
    BSString name;     // 08
};
static_assert(sizeof(BGSStoryEventMember) == 0x18);

// An event type the engine registers (CommonLibSSE-NG BGSRegisteredStoryEvent, a story node's ENAM).
struct BGSRegisteredStoryEvent
{
    uint32_t uniqueID;                       // 00, four characters: 'CLOC'
    uint32_t pad04;
    GameArray<BGSStoryEventMember>* members; // 08
    BSString name;                           // 10
    bool immediate;                          // 20
};
static_assert(sizeof(BGSRegisteredStoryEvent) == 0x28);

// CommonLibSSE-NG BGSStoryEventManager (22790): its event types, by their registered index.
struct BGSStoryEventManager
{
    static BGSStoryEventManager* Get() noexcept;

    GameArray<BGSRegisteredStoryEvent> registeredEvents; // 00
};
static_assert(offsetof(BGSStoryEventManager, registeredEvents) == 0);

// A player's story events, and whom the story manager judges each for (party-model.md rule 4,
// docs/re/systems/story-manager.md).
namespace StoryEvents
{
// CLOC's registered index (QuestEvents.h): what AddEvent_Impl (32359) is given for a location change.
[[nodiscard]] uint32_t LocationChangeIndex() noexcept;

// On while a guest: each story event this game raises is an OwnStoryEvent, for the host's story manager (the
// engine's quest starts aside).
void SetSends(bool aSends) noexcept;
// Its members as the session names them; none for one naming another game's actor (that game raises its own: a kill,
// an assault) or a type not here.
[[nodiscard]] std::optional<StoryEvent> ToSend(const OwnStoryEvent& acEvent) noexcept;
// Queued as the engine queues its player's, judged for aCharacter (a guest's on the host): its id, 0 when this game
// has no such type or its members do not fit it.
uint32_t Raise(const StoryEvent& acEvent, Actor& aCharacter) noexcept;

// What the story manager does with an event it processes (BGSStoryManager.cpp): skipped (an arrival the party already
// had, or its character gone), or judged for pJudged in place of this game's player (none: the player).
struct Verdict
{
    bool Process = true;
    Actor* pJudged = nullptr;
};
[[nodiscard]] Verdict Judge(const BGSStoryEvent& acEvent) noexcept;
// The event left the queue (processed, not timed out).
void Done(uint32_t aId) noexcept;
} // namespace StoryEvents
