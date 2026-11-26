#pragma once

#include <Games/Skyrim/DetectionRules.h>
#include <Messages/CrimeMessages.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// What the host's rules read and change in its game: crime, bounty and what the host's NPCs see. A part of HostGame.
struct HostCrimeGame
{
    // Native host adjudication of a guest action, and the party's per-faction bounty.
    std::function<bool(uint32_t, const CrimeRequest&)> JudgeCrime;
    std::function<std::vector<CrimeGold>()> ReadCrimeGold;
    std::function<std::vector<CrimeConfiscation>()> TakeCrimeConfiscations;
    std::function<std::vector<CrimeReport>()> TakeCrimeReports; // the bounties the host's engine reported (37679)
    // What the host's NPCs see of that character's copy now (41408, 46877; stealth.md "The eye is the host's"),
    // asking as the HUD asks of the host's own player; none when its copy is not loaded.
    std::function<std::optional<DetectionRules::EyeReading>(uint32_t aSessionId)> ReadEye;
};
