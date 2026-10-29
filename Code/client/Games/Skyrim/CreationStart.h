#pragma once

#include <Bridges/IJoinBridge.h>

// Our bundled, invite-only character start. All engine mutations run between main-loop frames.
namespace CreationStart
{
bool Begin() noexcept;
void Cancel() noexcept;
IJoinBridge::Creation Poll() noexcept;
} // namespace CreationStart
