#pragma once

struct HostRouter;

// Actor values: a hit lands in its owner's game, which then reports the resulting values to the others.
void RelayActorValues(HostRouter& aRouter) noexcept;
