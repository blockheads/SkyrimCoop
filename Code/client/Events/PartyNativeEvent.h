#pragma once

#include <Structs/ScriptValue.h>

// A script's call on this game's controls or screen (a fade, the camera, a shake, music, a video): every other game
// runs it too (PartyNatives, docs/design/cutscenes.md).
struct PartyNativeEvent
{
    LocalCall Call{};
};
