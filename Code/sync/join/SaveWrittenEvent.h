#pragma once

#include <string>

// The engine finished writing a save's file (109353): a quicksave or an autosave only now, after Save_Impl returned.
struct SaveWrittenEvent
{
    std::string Name;
};
