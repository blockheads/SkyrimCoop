#include <TiltedOnlinePCH.h>

#include <Games/ReferenceStates.h>

#include <TESObjectREFR.h>

namespace ReferenceStates
{
namespace
{
// Enable and Disable (19800, 19801) flip the disabled flag through TESForm::SetDisabled (14646). 1: enabled.
uint32_t ReadEnabled(const TESObjectREFR& acObject) noexcept
{
    return !acObject.IsDisabled();
}

// At once, no fade: the host's fade already played.
bool ApplyEnabled(TESObjectREFR& aObject, uint32_t aValue) noexcept
{
    if (aValue && aObject.IsDisabled())
        aObject.EnableImpl();
    else if (!aValue && !aObject.IsDisabled())
        aObject.DisableImpl();
    return true;
}

// The level in the low byte, kLocked above it; no lock reads as unlocked, level 0.
constexpr uint32_t kLocked = 0x100;

uint32_t ReadLock(const TESObjectREFR& acObject) noexcept
{
    const Lock* pLock = acObject.GetLock();
    return pLock ? (pLock->IsLocked() ? kLocked : 0) | pLock->lockLevel : 0;
}

bool ApplyLock(TESObjectREFR& aObject, uint32_t aValue) noexcept
{
    return aObject.SetLockState((aValue & kLocked) != 0, static_cast<uint8_t>(aValue & 0xFF));
}

constexpr ReferenceState cStates[] = {
    {ObjectChange::Kind::kEnabled, TESObjectREFR::CHANGE_FORM_FLAGS, &ReadEnabled, &ApplyEnabled},
    {ObjectChange::Kind::kLock, TESObjectREFR::CHANGE_OBJECT_EXTRA_LOCK, &ReadLock, &ApplyLock},
};
} // namespace

std::span<const ReferenceState> All() noexcept
{
    return cStates;
}

uint32_t ChangeFlags() noexcept
{
    static constexpr uint32_t cFlags = []
    {
        uint32_t flags = 0;
        for (const auto& cState : cStates)
            flags |= cState.ChangeFlag;
        return flags;
    }();
    return cFlags;
}

const ReferenceState* Find(ObjectChange::Kind aKind) noexcept
{
    for (const auto& cState : cStates)
        if (cState.Kind == aKind)
            return &cState;
    return nullptr;
}
} // namespace ReferenceStates
