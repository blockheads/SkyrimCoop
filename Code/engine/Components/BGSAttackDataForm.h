#pragma once

#include <Components/BaseFormComponent.h>
#include <NetImmerse/NiPointer.h>
#include <Primitives.h>

struct BGSAttackDataMap : NiRefObject
{
};

struct BGSAttackDataForm : BaseFormComponent
{
    NiPointer<BGSAttackDataMap> attackDataMap;
};
