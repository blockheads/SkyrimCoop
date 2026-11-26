#pragma once

// An actor's faction writes (AddToFaction 37686, RemoveFromFaction 37688, the rank setter 37677, SetCrimeFaction 37381,
// Papyrus RemoveFromAllFactions 54735) raised as FactionsChangeEvent; the engine sends no event for them. On while
// anyone started them (HostEngine, GuestEngine).
namespace FactionEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace FactionEvents
