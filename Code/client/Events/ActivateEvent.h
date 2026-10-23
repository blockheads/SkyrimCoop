#pragma once

#include <cstdint>

/**
 * @brief In-game activation (i.e. interacting with an object), as the hook on TESObjectREFR::Activate read it.
 */
struct ActivateEvent
{
    uint32_t ObjectFormId{0};
    uint32_t ActivatorFormId{0};
    // The activator is a reference, not an actor: a script opened a door (Activate(Self)).
    bool ByReference{false};
    uint32_t ObjectParentCellFormId{0};
    // A door's TESObjectREFR::OpenState before the activation.
    uint8_t PreActivationOpenStateValue{0};
    // Activate again through the bridge (ObjectService::OnActivate).
    bool ActivateFlag{false};
};
