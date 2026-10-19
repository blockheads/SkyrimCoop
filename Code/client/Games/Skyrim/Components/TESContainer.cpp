#include "TESContainer.h"

int64_t TESContainer::GetItemCount(TESForm* apItem) const noexcept
{
    // (container, item): the item's count among the base container's entries.
    TP_THIS_FUNCTION(TGetItemCount, int64_t, const TESContainer, TESForm* apItem);

    POINTER_SKYRIMSE(TGetItemCount, s_getItemCount, 14529);

    return TiltedPhoques::ThisCall(s_getItemCount, this, apItem);
}
