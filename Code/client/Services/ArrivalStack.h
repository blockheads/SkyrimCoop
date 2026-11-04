#pragma once

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

// The party's arrival spot: the players and any actor landing on it (a follower, its copy) pass through each
// other until each walks off it (docs/design/host-authority-migration.md, "Arriving together").
struct ArrivalStack
{
    using Clock = std::chrono::steady_clock;
    static constexpr float kOnSpot = 60.f;                                // within a step of the spot is on it
    static constexpr auto kArrival = std::chrono::seconds(5);             // a player coming from afar has this long
    static constexpr auto kWalkOff = std::chrono::milliseconds(500);      // off the spot this long has walked off

    struct Member
    {
        uint32_t FormId{};
        bool Player{};                // expected: it leaves only by walking off or never coming
        bool Arrived{};               // has stood on the spot
        Clock::time_point OffSince{}; // in this cell off the spot since (none: on it, or away)
    };

    ArrivalStack(const glm::vec3& acSpot, Clock::time_point aNow, const std::vector<uint32_t>& acPlayers) noexcept
        : Spot(acSpot), Since(aNow)
    {
        for (const uint32_t cPlayer : acPlayers)
            Members.push_back({cPlayer, true});
    }

    // Where the players and every other actor here stand; returns who passes through whom (on the spot or coming).
    std::vector<uint32_t> Update(const std::unordered_map<uint32_t, glm::vec3>& acPlayers,
                                 const std::unordered_map<uint32_t, glm::vec3>& acOthers, Clock::time_point aNow) noexcept
    {
        const bool cLate = aNow - Since > kArrival;
        // An actor landing on the spot while the party arrives joins it (a follower brought along, its copy).
        if (!cLate)
            for (const auto& [cFormId, cAt] : acOthers)
                if (OnSpot(cAt) && std::ranges::find(Left, cFormId) == Left.end() &&
                    std::ranges::none_of(Members, [&](const Member& acMember) { return acMember.FormId == cFormId; }))
                    Members.push_back({cFormId, false});

        std::erase_if(Members, [&](Member& aMember) {
            const auto& cHere = aMember.Player ? acPlayers : acOthers;
            const auto cAt = cHere.find(aMember.FormId);
            const bool cOff = cAt != cHere.end() && !OnSpot(cAt->second);
            aMember.Arrived = aMember.Arrived || (cAt != cHere.end() && !cOff);
            if (!aMember.Arrived)
            {
                if (cLate)
                    spdlog::info("Stack: {:X} never came", aMember.FormId);
                return cLate;
            }
            // An actor gone from here (unloaded, deleted) has left; a player away is still coming back.
            if (!aMember.Player && cAt == cHere.end())
            {
                Left.push_back(aMember.FormId);
                return true;
            }
            if (!cOff)
            {
                aMember.OffSince = {};
                return false;
            }
            if (aMember.OffSince == Clock::time_point{})
                aMember.OffSince = aNow;
            if (aNow - aMember.OffSince < kWalkOff)
                return false;
            spdlog::info("Stack: {:X} walked off ({:.0f} from the spot)", aMember.FormId,
                         glm::distance(glm::vec2(cAt->second), glm::vec2(Spot)));
            Left.push_back(aMember.FormId);
            return true;
        });

        std::vector<uint32_t> passing;
        for (const Member& cMember : Members)
            if (cMember.OffSince == Clock::time_point{})
                passing.push_back(cMember.FormId);
        return passing;
    }

    [[nodiscard]] bool Empty() const noexcept { return Members.empty(); }

    [[nodiscard]] bool OnSpot(const glm::vec3& acAt) const noexcept
    {
        return glm::distance(glm::vec2(acAt), glm::vec2(Spot)) < kOnSpot;
    }

    glm::vec3 Spot{};
    Clock::time_point Since;
    std::vector<Member> Members;
    std::vector<uint32_t> Left; // actors that walked off: they never join again
};
