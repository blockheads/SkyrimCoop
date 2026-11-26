#include <join/GuestSaves.h>

#include <join/IJoinBridge.h>
#include <Messages/JoinMessages.h>
#include <join/SaveStore.h>
#include <core/TransportService.h>
#include <core/World.h>

GuestSaves::GuestSaves(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_bridge(aBridge)
{
    Listen<NotifySaveChunk, &GuestSaves::OnSaveChunk>(m_connections, aDispatcher, this);
}

void GuestSaves::OnSaveChunk(const NotifySaveChunk& acChunk) noexcept
{
    // One this game loads now is JoinService's.
    if (!acChunk.Ahead)
        return;
    if (!m_save.Add(acChunk))
    {
        m_save = {};
        return;
    }
    if (!m_save.Complete())
        return;
    m_bridge.OffThread([&bridge = m_bridge, stream = std::exchange(m_save, {}).Get(), &world = m_world,
                        pAlive = std::weak_ptr(m_pAlive)]() -> std::function<void()> {
        const auto cImage = SaveStore::Decode(bridge, stream);
        const bool cHeld = cImage && SaveStore::Keep(bridge, stream.Save, *cImage);
        return [cHeld, cSize = stream.Data.size(), &bridge, &world, pAlive] {
            if (pAlive.expired())
                return;
            if (cHeld)
                spdlog::info("GuestSaves: holds a save the host made ({} bytes came)", cSize);
            else
                spdlog::warn("GuestSaves: a save sent ahead ({} bytes) could not be held", cSize);
            // Either way the host learns what is held now (a save it was encoded against may be gone).
            SaveHeldRequest held;
            held.Saves = bridge.HeldSaves();
            world.GetTransport().Send(held);
        };
    });
}
