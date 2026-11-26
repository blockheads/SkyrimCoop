#pragma once

#include <cstdint>
#include <string>

struct HostNetwork;

// Hosting a session, as the features see it: start and stop, the listener, a host's load, an id for a new actor.
// HostService is the one that does it.
struct Hosting
{
    virtual ~Hosting() = default;

    // Listens on aPort, or the next free one above it.
    virtual bool StartHosting(uint16_t aPort = 10578) noexcept = 0;
    virtual void StopHosting() noexcept = 0;
    // Null when not hosting.
    [[nodiscard]] virtual HostNetwork* GetNetwork() const noexcept = 0;
    // A host quickload or menu load: the chosen save goes through the party reload.
    virtual void LoadSave(const std::string& acName) noexcept = 0;
    // A session id for one of this game's actors while hosting, else 0.
    [[nodiscard]] virtual uint32_t NewCharacterId() noexcept = 0;
};
