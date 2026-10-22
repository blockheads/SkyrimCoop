#pragma once

#include <Structs/Tints.h>

struct FaceGenComponent
{
    FaceGenComponent() noexcept {}

    bool Generated{false};
    Tints FaceTints{};
};
