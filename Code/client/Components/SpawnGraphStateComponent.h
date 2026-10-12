#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

// A fresh copy's owner's graph state from the spawn: loaded once its 3D is set up, then removed (AnimationSystem).
struct SpawnGraphStateComponent
{
    String State;
};
