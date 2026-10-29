#pragma once

#include <cstdint>

struct Actor;
struct TESForm;
struct TESObjectREFR;

// A queued story event (CommonLibSSE-NG BGSStoryEvent), built from its raw event by 32330: each member a reference
// handle or a number in its low 32 bits, or a form.
struct BGSStoryEvent
{
    uint32_t id;         // 00, AddEvent_Impl's (32359)
    uint32_t index;      // 04, its type's registered index
    uint64_t members[6]; // 08
};
static_assert(sizeof(BGSStoryEvent) == 0x38);

// The story events a player's character raises, and whom the story manager judges each for (party-model.md rule 4,
// docs/re/systems/story-manager.md).
namespace StoryEvents
{
// CLOC, as PlayerCharacter::SetParentCell builds it (32288).
struct LocationChange
{
    uint32_t ActorHandle;  // 00
    uint32_t Pad04;
    TESForm* pOldLocation; // 08
    TESForm* pNewLocation; // 10
};
static_assert(sizeof(LocationChange) == 0x18);

// CRFT (CommonLibSSE-NG BGSCraftItemEvent).
struct CraftItem
{
    uint32_t WorkbenchHandle; // 00
    uint32_t Pad04;
    TESForm* pBenchLocation;  // 08
    TESForm* pCreated;        // 10
};
static_assert(sizeof(CraftItem) == 0x18);

// KILL, as Actor::KillImpl builds it (32282).
struct KillActor
{
    uint32_t VictimHandle; // 00
    uint32_t KillerHandle; // 04
    TESForm* pLocation;    // 08
    uint32_t Relationship; // 10
    uint32_t Crime;        // 14
};
static_assert(sizeof(KillActor) == 0x18);

// CLOC's registered index (QuestEvents.h): what AddEvent_Impl (32359) is given for a location change.
[[nodiscard]] uint32_t LocationChangeIndex() noexcept;

// Queued as the engine queues its player's, judged for aCharacter (a guest's on the host): its id.
uint32_t RaiseLocationChange(Actor& aCharacter, TESForm* apOldLocation, TESForm* apNewLocation) noexcept;
uint32_t RaiseCraft(Actor& aCharacter, TESObjectREFR* apWorkbench, TESForm* apLocation, TESForm* apCreated) noexcept;

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

// Other players' copies' location changes raised as CopyLocationChangeEvent, while anyone started them (HostEngine).
void Start() noexcept;
void Stop() noexcept;
} // namespace StoryEvents
