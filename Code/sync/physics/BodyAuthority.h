#pragma once

#include <Structs/BodyFrame.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// Who simulates each body, after Fiedler's "Networked Physics in Virtual Reality" (docs/design/physics-authority-plan.md):
//
//   a player's touch (push, hit, drop) takes a body nobody else holds       AuthSeq + 1
//   a grab takes it and holds it: nobody else may take it                   HoldSeq + 1
//   a body under a player touches one nobody has: that one too, recursively AuthSeq + 1
//   a player's pile, every body still, rests together                       AuthSeq + 1, AtRest
//
// The host arbitrates with Accept; a guest hears the host's verdict with Hear. Players are numbers, 0 nobody (the
// host's game simulates it). No engine: UnitTests.
class BodyAuthority
{
public:
    static constexpr uint32_t kNobody = 0;

    struct Claim
    {
        uint32_t Player{};
        uint16_t AuthSeq{};
        uint16_t HoldSeq{};
        bool Held{};
        bool AtRest{};

        bool operator==(const Claim&) const noexcept = default;
    };

    [[nodiscard]] Claim Of(const BodyKey& acKey) const noexcept;
    [[nodiscard]] bool Is(const BodyKey& acKey, uint32_t aPlayer) const noexcept { return Of(acKey).Player == aPlayer; }
    // The bodies aPlayer has authority over.
    [[nodiscard]] std::vector<BodyKey> Bodies(uint32_t aPlayer) const noexcept;

    // aPlayer's own push, hit or drop: false if another player holds it.
    bool Touch(const BodyKey& acKey, uint32_t aPlayer, uint64_t aTick) noexcept;
    // aPlayer's grab: false if another player holds it.
    bool Grab(const BodyKey& acKey, uint32_t aPlayer, uint64_t aTick) noexcept;
    // Let go: still aPlayer's until it rests.
    void Release(const BodyKey& acKey, uint32_t aPlayer) noexcept;
    // Bodies nobody has that touch one of aPlayer's moving bodies (acContacts: pairs Havok made), taken recursively. Havok
    // pairs bodies whose AABBs overlap (64648), before any touch: one is taken only once acMoved says it left where it lay.
    std::vector<BodyKey> Spread(uint32_t aPlayer, const std::vector<std::pair<BodyKey, BodyKey>>& acContacts, uint64_t aTick,
                                const std::function<bool(const BodyKey&)>& acMoved = {}) noexcept;
    // aPlayer's bodies not held, grouped by contact: a group rests once every body in it is still (acStill) and none
    // was touched within kLeanMs, or once it has been still kRestWaitMs. The bodies that rest now, at rest, still aPlayer's.
    std::vector<BodyKey> Rest(uint32_t aPlayer, const std::set<BodyKey>& acStill, const std::vector<std::pair<BodyKey, BodyKey>>& acContacts,
                              uint64_t aTick) noexcept;

    // The host: whether aPlayer's state of a body stands (its claim kept), its rest given back to nobody.
    bool Accept(uint32_t aPlayer, const BodyState& acState) noexcept;
    // The host: nobody's again, the number moved on (the host's game placed it, or its authority went silent).
    void Unclaim(const BodyKey& acKey) noexcept;
    // The host's game read it moving (a body nobody or its player has).
    void Moved(const BodyKey& acKey) noexcept;
    // The host's game read it at rest: nobody's at once, a player's number moved on (the host's game is the arbiter).
    void Rested(const BodyKey& acKey) noexcept;

    // A guest: the host's state of a body. Old: this game's own claim stands, the state is not applied.
    enum class Heard
    {
        kOld,
        kApply,     // the host's claim stands: its state is shown here
        kConfirmed, // this game's rest, confirmed: nobody's now, nothing to apply
    };
    Heard Hear(const BodyState& acState, uint32_t aMe) noexcept;

    // The state of a body as aReceiver hears it (who it is, from its side).
    [[nodiscard]] BodyState Stamp(const BodyFrame& acFrame, uint32_t aReceiver) const noexcept;

    void Forget(const BodyKey& acKey) noexcept { m_bodies.erase(acKey); }
    void Clear() noexcept { m_bodies.clear(); }

private:
    struct Entry
    {
        Claim Is{};
        uint64_t Touched{};
        std::optional<uint64_t> StillSince; // its group's, while waiting to rest
    };
    // a's numbers newer than b's: a newer HoldSeq wins over any AuthSeq.
    [[nodiscard]] static bool Newer(const Claim& acA, const Claim& acB) noexcept;
    [[nodiscard]] static bool Same(const Claim& acA, const Claim& acB) noexcept
    {
        return acA.HoldSeq == acB.HoldSeq && acA.AuthSeq == acB.AuthSeq;
    }

    std::map<BodyKey, Entry> m_bodies;
};
