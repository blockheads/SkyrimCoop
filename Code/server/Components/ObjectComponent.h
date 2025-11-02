#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/LockData.h>
#include <Game/Player.h>

namespace Server
{

struct ObjectComponent
{
    ObjectComponent(Player* apLastSender)
        : pLastSender(apLastSender)
    {
    }

    Player* pLastSender;
    LockData CurrentLockData{};
};

} // namespace Server
