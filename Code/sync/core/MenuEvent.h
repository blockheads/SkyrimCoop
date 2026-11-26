#pragma once

#include <string>

// A menu of the engine's opened or closed (UI's MenuOpenCloseEvent): "Main Menu", "RaceSex Menu", ...
struct MenuEvent
{
    std::string Menu;
    bool Opening{};
};
