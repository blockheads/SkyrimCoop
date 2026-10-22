#pragma once

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

    TP_NOCOPYMOVE(CommandService);

protected:
    void OnSetTimeCommand(const SetTimeCommandEvent&) const noexcept;

private:
    TransportService& m_transport;

    entt::scoped_connection m_setTimeConnection;
};
