#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>

// The party's arrivals (party-model.md rule 4): players arriving at one location are one story event, the first's.
struct PartyArrivals
{
    // aPlayer's location change story event, in the order the story manager processes them: true when it is the
    // party's (no other player arrived at aLocation and is still there), false for one the party already had.
    // acIsPlayer: whether a form id is still a player's character here (one who left the session is not).
    [[nodiscard]] bool Arrive(uint32_t aPlayer, uint32_t aLocation, const std::function<bool(uint32_t)>& acIsPlayer) noexcept;

private:
    std::unordered_map<uint32_t, uint32_t> m_at; // each player's location, from its last arrival
};
