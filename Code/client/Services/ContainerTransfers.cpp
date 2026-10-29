#include <Bridges/IPlayerBridge.h>
#include <FormIds.h>
#include <Messages/ContainerTransferRequest.h>
#include <Services/ContainerTransfers.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <SessionIds.h>
#include <World.h>

namespace ContainerTransfers
{
bool Propose(World& aWorld, uint32_t aSource, uint32_t aTarget, Inventory::Entry aItem, bool aSteal) noexcept
{
    const uint32_t cPlayer = aWorld.ctx().at<IPlayerBridge&>().GetPlayerFormId();
    if (!aSource || !aTarget || aItem.Count <= 0 || (aSource != cPlayer && aTarget != cPlayer) ||
        !aWorld.GetTransport().IsOnline())
        return false;
    const bool cTake = aTarget == cPlayer;
    const uint32_t cContainer = cTake ? aSource : aTarget;
    aItem = Stack(std::move(aItem));
    aItem.Count *= cTake ? 1 : -1;
    aWorld.GetRunner().Queue([&aWorld, cContainer, cItem = std::move(aItem), aSteal] {
        ContainerTransferRequest request;
        if (const auto cActor = Utils::GetSessionIdByFormId(cContainer))
            request.ContainerActorId = *cActor;
        else if (const auto cObject = ToSession(cContainer))
            request.Container = *cObject;
        else
            return;
        request.Item = cItem;
        request.Steal = aSteal;
        aWorld.GetTransport().Send(request);
    });
    return true;
}
} // namespace ContainerTransfers
