#pragma once

// Hits with no actor aggressor: an object's projectile, a causeless explosion, a body's impact (ProcessHit 38586).
namespace WorldHits
{
// The hit on this thread is the world's (its DoDamage 37335 runs inside 38586): on another player's copy the host's
// verdict for that player's game, on a guest's own player its copy of it (HitRules::Of).
[[nodiscard]] bool Current() noexcept;

// Hits made in its scope are this game's own to judge (the host's relayed trap hit, its player's impact), marked in the
// hit itself (HitData::kJudgedHere), which ProcessHit's queue copies.
struct JudgedHere
{
    JudgedHere() noexcept;
    ~JudgedHere() noexcept;
    JudgedHere(const JudgedHere&) = delete;
    JudgedHere& operator=(const JudgedHere&) = delete;

private:
    bool m_before;
};
} // namespace WorldHits
