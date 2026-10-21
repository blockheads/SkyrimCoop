#pragma once

// The engine's container changes (TESContainerChangedEvent) raised as ContainerChangeEvent, one per reference
// involved; our own (under ScopedInventoryOverride) left out. On while anyone started them (HostEngine, GuestEngine).
namespace ContainerEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace ContainerEvents
