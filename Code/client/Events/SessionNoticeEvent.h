#pragma once

#include <string>

// A line about the session for this game's player: hosting, someone joining or leaving, a join failing. The chat shows
// it as a system line.
struct SessionNoticeEvent
{
    std::string Text;
};
