#pragma once

#include "Forms/TESForm.h"
#include <Primitives.h>

struct BGSListForm : TESForm
{
    GameArray<TESForm*> ArrayOfForms{};
    GameArray<uint32_t>* pScriptAddedTempFormA{};
    uint32_t iScriptAddedFormCount{};
};

static_assert(sizeof(BGSListForm) == 0x48);
