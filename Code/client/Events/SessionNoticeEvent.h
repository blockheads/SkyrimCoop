#pragma once

#include <string>

// A line about the session for this game's player: hosting, someone joining, leaving or going down, a join failing.
// The chat shows it as a system line.
struct SessionNoticeEvent
{
    std::string Text;
    // A player went down: the chat shows it in red.
    bool Perished = false;
};
