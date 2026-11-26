#pragma once

#include <Structs/RecordPart.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

// Every actor's last declared state, in versioned parts by session id; a part waits for its copy and for the actors it
// names (docs/design/one-simulator.md, piece 1).
struct CopyRecords
{
    // Puts a part on the copy of aId. Returns the session ids its bytes name that are not on this game yet; empty when
    // applied, and when aId itself has no copy here (OnSessionId applies it then).
    using Apply_t = std::function<std::vector<uint32_t>(uint32_t aId, const std::string& acBytes)>;
    // One of this game's actors' part as it is now (none: nothing to declare).
    using Read_t = std::function<std::optional<std::string>(uint32_t aFormId)>;
    using Send_t = std::function<void(uint32_t aId, const RecordPart& acPart)>;
    using Ask_t = std::function<void(uint32_t aId, RecordKind aKind)>;

    // A kind of part, once, by its service.
    void Register(RecordKind aKind, Apply_t aApply, Read_t aRead = {});
    // Where an owner's declared part goes (the host, for every other game).
    void SendWith(Send_t aSend) { m_send = std::move(aSend); }
    // Where a part that cannot be merged asks its owner for the whole one (R3).
    void AskWith(Ask_t aAsk) { m_ask = std::move(aAsk); }

    // This game owns aId and its part changed: kept under the next version (a kItems change merged) and sent as given.
    void Declare(uint32_t aId, RecordKind aKind, std::string aBytes);
    // This game announces its actor aFormId as aId: every kind read and kept, for the announcement to carry.
    [[nodiscard]] RecordParts ReadAll(uint32_t aId, uint32_t aFormId);
    // The part this game holds goes out again under a new version (another game asked, R3). False: none held.
    bool DeclareAgain(uint32_t aId, RecordKind aKind);

    // A part from aId's owner. False: not newer than the one held, or a change that cannot be merged (its owner asked).
    bool Take(uint32_t aId, const RecordPart& acPart);
    // The actor of aId is on this game now (a copy made, or an actor a part names): its parts apply, and those of
    // the actors waiting for it.
    void OnSessionId(uint32_t aId);
    // aId's part of aKind applied again now (its copy changed by its own engine).
    void ApplyAgain(uint32_t aId, RecordKind aKind) { TryApply(aId, aKind); }
    // aId's part of aKind is applied again when aNamed appears (an Apply that ran later than Take, its stream behind).
    void WaitFor(uint32_t aId, RecordKind aKind, uint32_t aNamed);
    // The actor left the session.
    void Forget(uint32_t aId);
    void Clear();

    [[nodiscard]] const RecordPart* Find(uint32_t aId, RecordKind aKind) const noexcept;
    [[nodiscard]] RecordParts PartsOf(uint32_t aId) const;
    // Whether aId's part of aKind is waiting for the actor aNamed.
    [[nodiscard]] bool WaitsFor(uint32_t aId, RecordKind aKind, uint32_t aNamed) const noexcept;
    // Every held part of aKind: aVisit(session id, bytes).
    template <class TVisit> void ForEach(RecordKind aKind, TVisit&& aVisit) const
    {
        for (const auto& [cId, cParts] : m_records)
            if (const auto it = cParts.find(aKind); it != cParts.end())
                aVisit(cId, it->second.Bytes);
    }

private:
    struct Kind
    {
        Apply_t Apply;
        Read_t Read;
    };
    using Waiter = std::pair<uint32_t, RecordKind>;

    RecordPart& Next(uint32_t aId, RecordKind aKind, std::string aBytes);
    void TryApply(uint32_t aId, RecordKind aKind);

    std::map<RecordKind, Kind> m_kinds;
    std::unordered_map<uint32_t, std::map<RecordKind, RecordPart>> m_records;
    // By the session id named: the parts to apply again when it appears.
    std::unordered_map<uint32_t, std::set<Waiter>> m_waitsFor;
    Send_t m_send;
    Ask_t m_ask;
};
