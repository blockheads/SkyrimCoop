#pragma once

// The engine starting or stopping an effect's hit visuals (34094, ours: ActiveEffect::StartHitEffects;
// ActiveEffect::StopHitEffects 34050) raised as EffectVisualsChangeEvent. On while anyone started them (HostEngine,
// GuestEngine).
namespace EffectEvents
{
void Start() noexcept;
void Stop() noexcept;
} // namespace EffectEvents
