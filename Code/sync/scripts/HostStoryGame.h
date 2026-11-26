#pragma once

#include <Messages/StoryEventRequest.h>

#include <cstdint>
#include <functional>

// What the host's rules read and change in its game: story events for the host's story manager. A part of HostGame.
struct HostStoryGame
{
    // A guest's story event, queued in the host's story manager and judged for the guest's character (HostStory).
    std::function<void(const StoryEvent& acEvent)> RaiseStoryEvent;
};
