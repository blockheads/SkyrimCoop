#pragma once

#include <Messages/EnterCellRequest.h>

// The player arrived somewhere new (a door, fast travel, a MoveTo, coc; TravelService): its copies of the
// others went with the old cell.
struct PlayerArrivedEvent
{
    uint32_t CellId{};
    uint32_t WorldSpaceId{}; // 0: an interior
    EnterCellRequest::Travel How{};
};
