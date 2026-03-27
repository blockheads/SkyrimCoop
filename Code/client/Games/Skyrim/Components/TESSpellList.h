#pragma once

#include <Components/BaseFormComponent.h>

struct SpellItem;
struct TESForm;
struct TESShout;

struct TESSpellList : BaseFormComponent
{
    struct Lists
    {
        SpellItem** spells{nullptr};
        TESForm** unk4{nullptr};
        TESShout** shouts{nullptr};
        uint32_t spellCount{0};
        uint32_t unk4Count{0};
        uint32_t shoultCount{0};
    };

    Lists* lists;

    void Initialize();
};

SKYRIM_STRUCT_ASSERT(sizeof(TESSpellList::Lists) == 0x28);
