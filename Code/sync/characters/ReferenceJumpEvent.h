#pragma once

#include <cstdint>

/**
 * @brief A reference put somewhere at once (a script's MoveTo or SetPosition, the console's setpos or setangle),
 * not moved there: its copies jump with it.
 */
struct ReferenceJumpEvent
{
    uint32_t FormId{};
};
