#include <Services/Host/HostValues.h>

#include <Services/Host/HostRouter.h>
#include <Messages/Relayed.h>

#include <Messages/RelayedMessages.h>

void RelayActorValues(HostRouter& aRouter) noexcept
{
    using enum HostRouter::Delivery;
    aRouter.Relay<RequestActorValueChanges, NotifyActorValueChanges>(FromOwner, &RequestActorValueChanges::Id);
    aRouter.Relay<RequestActorMaxValueChanges, NotifyActorMaxValueChanges>(FromOwner, &RequestActorMaxValueChanges::Id);
    aRouter.Relay<RequestDeathStateChange, NotifyDeathStateChange>(FromOwner, &RequestDeathStateChange::Id);
    aRouter.Relay<RequestHealthChangeBroadcast, NotifyHealthChangeBroadcast>(ToOwner, &RequestHealthChangeBroadcast::Id);
}
