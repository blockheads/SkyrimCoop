#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>

namespace TestHarness
{
// The engine's script events as they happen (the ones Papyrus's OnSpellCast, OnMagicEffectApply,
// OnEffectStart/OnEffectFinish, OnHit, quest start and stop, and OnObjectEquipped come from), numbered, the
// last 512 kept: what a scenario checks when it has to see that a step happened.
struct EngineEvents
{
    // Listens once the engine's event sources exist. Once a frame.
    void Poll() noexcept;
    // Lets go of the event sources (before this DLL goes).
    void Stop() noexcept;
    // The events numbered aSince and later, and the next number.
    nlohmann::json Since(uint64_t aSince) const;

private:
    bool m_listening = false;
};
} // namespace TestHarness
