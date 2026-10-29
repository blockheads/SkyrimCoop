#pragma once

#include <Messages/StoryEventRequest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

// A story event of any registered type, between its engine form and the session's (docs/re/systems/story-manager.md,
// "A guest's story events"). Checked by ClientTests.
namespace StoryEventRules
{
// A member's type (CommonLibSSE-NG BGSStoryEventMember::DATA_TYPE).
enum class MemberType : uint32_t
{
    kReference, // a reference handle, 4 bytes in the raw event
    kForm,      // the others pointers, 8 bytes, 8-aligned
    kLocation,
    kInteger, // 4 bytes
    kKeyword,
};

// What a reference member names in this game.
struct Named
{
    enum Kind : uint8_t
    {
        kNothing,    // no reference, or one the session cannot name
        kThisPlayer, // this game's player
        kActor,      // another actor: a copy here, its game raises its own events (a kill, an assault)
        kObject,     // a world reference
    };
    Kind Is = kNothing;
    GameId Id{};
};

// The members this game's event sends the host (BGSStoryEvent's, widened to 64 bits each); none for an event naming
// another game's actor.
[[nodiscard]] std::optional<std::vector<StoryEvent::Member>> ToSend(std::span<const MemberType> aTypes, std::span<const uint64_t> aMembers,
                                                                    const std::function<Named(uint32_t aHandle)>& acReference,
                                                                    const std::function<std::optional<GameId>(uint64_t aForm)>& acForm) noexcept;

// The raw event the engine's build (32330) reads for these member values: 4-byte references and integers, 8-byte
// pointers aligned to 8. None when there are more values than types allow.
using Raw = std::array<uint64_t, StoryEvent::kMaxMembers>;
[[nodiscard]] std::optional<Raw> Layout(std::span<const MemberType> aTypes, std::span<const uint64_t> aValues) noexcept;

// A member sent of a kind that fits its type here (a reference for a reference, a form for a form...).
[[nodiscard]] bool Fits(MemberType aType, StoryEvent::Member::Kind aKind) noexcept;

// A type id as its four characters ("CLOC"), for logs.
[[nodiscard]] std::string TypeName(uint32_t aType) noexcept;
} // namespace StoryEventRules
