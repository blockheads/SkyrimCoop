#pragma once

#include <animation/ReplayState.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

// The host's answers to action claims as this game heard them (ClaimService), by the acting actor's session id and the
// action's tick: what a copy's replayed kill move and a held blow go by (AnimationSystem, ActorValueService).
struct ActionClaims
{
    struct Answer
    {
        uint64_t Tick{};
        uint32_t ClaimId{};              // 0: refused
        std::vector<uint32_t> Members{}; // session ids
        bool Claimed{};                  // false: refused, ended, or replaced
        uint8_t Kind{};
    };

    // Kept by action tick: a copy's replay behind its owner still finds an earlier answer.
    void Set(uint32_t aActorId, Answer aAnswer) noexcept
    {
        // One claim per actor: a grant naming a member replaces any claim it was in (a passenger driving on).
        if (aAnswer.Claimed)
            for (auto& [cActorId, kept] : m_answers)
                for (Answer& answer : kept)
                    if (answer.Claimed && std::ranges::any_of(answer.Members, [&](uint32_t aId) {
                            return std::ranges::find(aAnswer.Members, aId) != aAnswer.Members.end();
                        }))
                        answer.Claimed = false;
        auto& answers = m_answers[aActorId];
        std::erase_if(answers, [&](const Answer& acKept) { return acKept.Tick == aAnswer.Tick; });
        if (answers.size() == cKept)
            answers.erase(answers.begin());
        answers.push_back(std::move(aAnswer));
    }
    // The claim ended (its members given back): a replay of its action arriving later is not played.
    void End(uint32_t aClaimId) noexcept
    {
        for (auto& [cActorId, answers] : m_answers)
            for (Answer& answer : answers)
                if (aClaimId && answer.ClaimId == aClaimId)
                    answer.Claimed = false;
    }
    void Clear() noexcept { m_answers.clear(); }

    // The claim of the action aActorId took at aTick.
    [[nodiscard]] ReplayState::ClaimAnswer Of(uint32_t aActorId, uint64_t aTick) const noexcept
    {
        const auto cAnswers = m_answers.find(aActorId);
        if (cAnswers == m_answers.end())
            return ReplayState::ClaimAnswer::kUnknown;
        const auto cAnswer = std::ranges::find(cAnswers->second, aTick, &Answer::Tick);
        if (cAnswer == cAnswers->second.end())
            return ReplayState::ClaimAnswer::kUnknown;
        return cAnswer->Claimed ? ReplayState::ClaimAnswer::kClaimed : ReplayState::ClaimAnswer::kRefused;
    }

    // aActorId acts in a live claim of aKind on aMemberId (a kill move: a forwarded hit between them is its blow).
    [[nodiscard]] bool Holds(uint32_t aActorId, uint32_t aMemberId, uint8_t aKind) const noexcept
    {
        const auto cAnswers = m_answers.find(aActorId);
        return cAnswers != m_answers.end() && std::ranges::any_of(cAnswers->second, [&](const Answer& acAnswer) {
                   return acAnswer.Claimed && acAnswer.Kind == aKind && std::ranges::find(acAnswer.Members, aMemberId) != acAnswer.Members.end();
               });
    }

private:
    static constexpr size_t cKept = 8; // answers per actor, the oldest dropped
    std::unordered_map<uint32_t, std::vector<Answer>> m_answers;
};
