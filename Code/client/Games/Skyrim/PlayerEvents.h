#pragma once

// The player's race switches (TESSwitchRaceCompleteEvent) and level writes (TESActorBaseData::SetLevel 14385) raised
// as PlayerFormChangeEvent. On while anyone started them (HostEngine, GuestEngine).
namespace PlayerEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace PlayerEvents
