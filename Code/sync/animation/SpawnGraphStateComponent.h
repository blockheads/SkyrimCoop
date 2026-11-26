#pragma once

// A fresh copy's owner's graph state from the spawn: loaded once its 3D is set up, then removed (AnimationSystem).
struct SpawnGraphStateComponent
{
    std::string State;
};
