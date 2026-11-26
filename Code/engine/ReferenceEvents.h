#pragma once

// The engine's 3D loads (TESObjectLoadedEvent) raised as ReferenceLoadedEvent. On while anyone started them
// (HostEngine, GuestEngine).
namespace ReferenceEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace ReferenceEvents
