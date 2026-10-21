#pragma once

// The engine's cell loads (TESCellFullyLoadedEvent) raised as CellLoadedEvent. On while anyone started them
// (GuestEngine).
namespace CellEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace CellEvents
