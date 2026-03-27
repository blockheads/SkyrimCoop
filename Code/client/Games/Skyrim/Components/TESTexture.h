#pragma once

#include <Components/BaseFormComponent.h>
#include <Misc/BSFixedString.h>

struct TESTexture : BaseFormComponent
{
    virtual ~TESTexture(){};

    void Construct();

    BSFixedString name;
};

SKYRIM_STRUCT_ASSERT(offsetof(TESTexture, name) == 8);
