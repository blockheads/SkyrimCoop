#include <movement/HostMovement.h>

#include <characters/HostCharacters.h>
#include <core/Counters.h>
#include <core/HostNetwork.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>
#include <movement/SnapshotAge.h>

#include <Messages/Delivery.h>
#include <Messages/NotifyReferencesMove.h>
#include <Messages/ReferencesMoveRequest.h>

HostMovement::HostMovement(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(aCharacters)
{
    aRouter.On(*this, &HostMovement::OnMovement);
}

void HostMovement::OnMovement(const ReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept
{
    NotifyReferencesMove notify;
    // The owner's time, as its actions carry: copies replay both on one timeline; CopyClock never plays ahead.
    notify.Tick = acRequest.Tick;
    if (const uint64_t cNow = m_network.GetTick(); acRequest.Tick > cNow)
        COUNT("movement stamp ahead ms", acRequest.Tick - cNow); // a game's clock ahead of the host's (red-flags.md, 2)
    COUNT("movement messages", 1);
    for (const auto& [cSessionId, cUpdate] : acRequest.Updates)
    {
        if (m_characters.OwnerOf(cSessionId) != aSender)
        {
            spdlog::debug("HostMovement: {:x} moved {:x}, which it does not own", aSender, cSessionId);
            continue;
        }
        notify.Updates[cSessionId] = cUpdate;
        if (auto* pRecord = m_characters.PlayerRecordOf(cSessionId, aSender))
        {
            // Sent before its arrival but after it here: an old place.
            if (!SnapshotAge::IsStale(acRequest.Tick, pRecord->Tick))
            {
                pRecord->Tick = SnapshotAge::Cutoff(acRequest.Tick, m_network.GetTick(), pRecord->Tick);
                pRecord->CellId = cUpdate.UpdatedMovement.CellId;
                pRecord->Position = cUpdate.UpdatedMovement.Position;
                pRecord->Rotation = cUpdate.UpdatedMovement.Rotation;
            }
            if (!cUpdate.ActionEvents.empty())
                pRecord->LatestAction = cUpdate.ActionEvents.back();
        }
    }
    if (!notify.Updates.empty())
    {
        m_players.SendToOthers(notify, aSender, MustArrive(notify.Updates) ? Net::kReliable : Net::kUnreliable);
    }
}
