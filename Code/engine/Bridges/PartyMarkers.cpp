#include <Bridges/SkyrimBridge.h>
#include <Interface/PartyMarkers.h>

void SkyrimBridge::SetPartyMarkers(const PartyMarkerSet& acMarkers) noexcept
{
    PartyMarkerDisplay::Set(acMarkers);
}
