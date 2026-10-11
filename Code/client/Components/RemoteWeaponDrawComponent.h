#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

// On a copy: its owner's hands, as last reported (spawn data, then NotifyDrawWeapon). The copy
// replays the owner's draw/sheathe animations; when a replay is refused it converges to this.
struct RemoteWeaponDrawComponent
{
    bool HandsUp = false;
    double DriftSeconds = 0.0;
};
