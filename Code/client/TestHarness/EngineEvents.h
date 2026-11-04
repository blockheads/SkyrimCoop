#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>

namespace TestHarness
{
// The engine's script events (Papyrus's OnSpellCast, OnHit, OnObjectEquipped, quest start/stop, ...) and each save
// loaded, numbered, the last 512 kept: what a scenario checks to see that a step happened.
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
