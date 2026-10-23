#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

struct World;
struct TransportService;
struct SetTimeCommandEvent;

/**
 * @brief Responsible for processing, sending and receiving chat commands.
 *
 * Sends the commands the overlay's chat parses (set time).
 */
struct CommandService
{
public:
    CommandService(TransportService& aTransport, entt::dispatcher& aDispatcher);
    ~CommandService() noexcept = default;

    NO_COPY_MOVE(CommandService);

protected:
    void OnSetTimeCommand(const SetTimeCommandEvent&) const noexcept;

private:
    TransportService& m_transport;

    std::vector<entt::scoped_connection> m_connections;
};
