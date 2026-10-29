#pragma once

#include <array>
#include <cstdint>

// This game raised a story event (BGSStoryEvent's build, 32330) while a guest: for the host's story manager
// (StoryEvents::ToSend names its members for the session).
struct OwnStoryEvent
{
    uint32_t Index{};                    // its type's registered index here
    std::array<uint64_t, 6> Members{};   // BGSStoryEvent::members
};
