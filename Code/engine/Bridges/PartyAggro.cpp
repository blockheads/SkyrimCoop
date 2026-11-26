#include <Bridges/HostEngine.h>
#include <Combat/PartyAggro.h>

void HostEngine::SetPartyThreats(const PartyThreat::State& acState) const noexcept
{
    PartyAggroNative::Publish(acState);
}
