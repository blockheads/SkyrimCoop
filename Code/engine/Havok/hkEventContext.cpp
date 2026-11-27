#include <Havok/hkEventContext.h>

hkEventContext::hkEventContext(hkbCharacter* apCharacter, ahkpWorld* apWorld)
{
    character = apCharacter;
    behaviorGraph = nullptr;
    unk10 = nullptr;
    characterContext = nullptr;
    unk20 = nullptr;
    unk28 = nullptr;
    byte30 = 0;
    hkpWorld = apWorld;
    unk40 = nullptr;
    unk48 = nullptr;
}
