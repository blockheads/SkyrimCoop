#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

// On an actor this game owns: the hands' direction last sent to the server.
struct LocalWeaponDrawComponent
{
    bool HandsUp = false;
};
