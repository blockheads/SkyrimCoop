#include <Services/CopyRecords.h>

void CopyRecords::Register(RecordKind aKind, Apply_t aApply, Read_t aRead)
{
    m_kinds[aKind] = {std::move(aApply), std::move(aRead)};
}

RecordPart& CopyRecords::Next(uint32_t aId, RecordKind aKind, std::string aBytes)
{
    RecordPart& part = m_records[aId][aKind];
    const RecordPart cChange{aKind, part.Version + 1, std::move(aBytes)};
    // The owner's own change always follows the version it holds.
    part.Bytes = MergedPart(part.Version ? &part : nullptr, cChange).value_or(cChange.Bytes);
    part.Kind = aKind;
    part.Version = cChange.Version;
    return part;
}

void CopyRecords::Declare(uint32_t aId, RecordKind aKind, std::string aBytes)
{
    RecordPart sent{aKind, 0, aBytes};
    sent.Version = Next(aId, aKind, std::move(aBytes)).Version;
    if (m_send)
        m_send(aId, sent);
}

RecordParts CopyRecords::ReadAll(uint32_t aId, uint32_t aFormId)
{
    for (const auto& [cKind, cHow] : m_kinds)
        if (cHow.Read)
            if (auto bytes = cHow.Read(aFormId))
                Next(aId, cKind, std::move(*bytes));
    return PartsOf(aId);
}

bool CopyRecords::DeclareAgain(uint32_t aId, RecordKind aKind)
{
    const RecordPart* cpHeld = Find(aId, aKind);
    if (!cpHeld)
        return false;
    Declare(aId, aKind, cpHeld->Bytes);
    return true;
}

bool CopyRecords::Take(uint32_t aId, const RecordPart& acPart)
{
    RecordPart& part = m_records[aId][acPart.Kind];
    part.Kind = acPart.Kind;
    if (acPart.Version <= part.Version)
        return false;
    auto merged = MergedPart(part.Version ? &part : nullptr, acPart);
    if (!merged)
    {
        if (m_ask)
            m_ask(aId, acPart.Kind);
        return false;
    }
    part.Version = acPart.Version;
    part.Bytes = std::move(*merged);
    TryApply(aId, acPart.Kind);
    return true;
}

void CopyRecords::OnSessionId(uint32_t aId)
{
    // Copied: applying a part changes who waits for whom.
    std::vector<Waiter> parts;
    if (const auto it = m_records.find(aId); it != m_records.end())
        for (const auto& [cKind, cPart] : it->second)
            parts.emplace_back(aId, cKind);
    if (const auto it = m_waitsFor.find(aId); it != m_waitsFor.end())
        parts.insert(parts.end(), it->second.begin(), it->second.end());
    for (const auto& [cId, cKind] : parts)
        TryApply(cId, cKind);
}

void CopyRecords::WaitFor(uint32_t aId, RecordKind aKind, uint32_t aNamed)
{
    if (aNamed && aNamed != aId)
        m_waitsFor[aNamed].insert({aId, aKind});
}

void CopyRecords::TryApply(uint32_t aId, RecordKind aKind)
{
    const Waiter cWaiter{aId, aKind};
    std::erase_if(m_waitsFor, [&](auto& aEntry) {
        aEntry.second.erase(cWaiter);
        return aEntry.second.empty();
    });
    const RecordPart* cpPart = Find(aId, aKind);
    const auto cKind = m_kinds.find(aKind);
    if (!cpPart || !cpPart->Version || cKind == m_kinds.end() || !cKind->second.Apply)
        return;
    // Its bytes copied: Apply may reach a service that declares or takes another part.
    const std::string cBytes = cpPart->Bytes;
    for (const uint32_t cNamed : cKind->second.Apply(aId, cBytes))
        if (cNamed && cNamed != aId)
            m_waitsFor[cNamed].insert(cWaiter);
}

void CopyRecords::Forget(uint32_t aId)
{
    if (const auto it = m_records.find(aId); it != m_records.end())
    {
        for (const auto& [cKind, cPart] : it->second)
            std::erase_if(m_waitsFor, [&](auto& aEntry) {
                aEntry.second.erase(Waiter{aId, cKind});
                return aEntry.second.empty();
            });
        m_records.erase(it);
    }
}

void CopyRecords::Clear()
{
    m_records.clear();
    m_waitsFor.clear();
}

const RecordPart* CopyRecords::Find(uint32_t aId, RecordKind aKind) const noexcept
{
    const auto cRecord = m_records.find(aId);
    if (cRecord == m_records.end())
        return nullptr;
    const auto cPart = cRecord->second.find(aKind);
    return cPart != cRecord->second.end() && cPart->second.Version ? &cPart->second : nullptr;
}

RecordParts CopyRecords::PartsOf(uint32_t aId) const
{
    RecordParts parts;
    if (const auto it = m_records.find(aId); it != m_records.end())
        for (const auto& [cKind, cPart] : it->second)
            if (cPart.Version)
                parts.Parts.push_back(cPart);
    return parts;
}

bool CopyRecords::WaitsFor(uint32_t aId, RecordKind aKind, uint32_t aNamed) const noexcept
{
    const auto it = m_waitsFor.find(aNamed);
    return it != m_waitsFor.end() && it->second.contains({aId, aKind});
}
