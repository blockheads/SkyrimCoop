#pragma once

#include <Structs/Tints.h>

struct FaceGenComponent
{
    FaceGenComponent() noexcept {}

    bool Painted{false}; // these tints painted on a head (another head built since is not)
    uint32_t Paints{0};  // times painted, for the diagnostics
    Tints FaceTints{};
};
