#include "TESContainer.h"
#include <VersionDb.h>

int64_t TESContainer::GetItemCount(TESForm* apItem) const noexcept
{
    // (container, item): the item's count among the base container's entries.
    using TGetItemCount = int64_t(const TESContainer*, TESForm* apItem);

    POINTER_SKYRIMSE(TGetItemCount, s_getItemCount, 14529);

    return s_getItemCount(this, apItem);
}
