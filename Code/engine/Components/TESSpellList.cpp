#include <Components/TESSpellList.h>
#include <Memory.h>

void TESSpellList::Initialize()
{
    if (lists != nullptr)
        return;

    lists = Memory::New<Lists>();
}
