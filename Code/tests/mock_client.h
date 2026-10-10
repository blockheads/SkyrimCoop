#pragma once

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Platform.hpp>

#include <chrono>

#include <glm/glm.hpp>

#include <Client.hpp>
#include <Messages/Message.h>
#include <Messages/AuthenticationResponse.h>

namespace Server { struct GameServer; }

struct MockClient : TiltedPhoques::Client
{
    MockClient() noexcept;
    ~MockClient() override;

    void SendGameMessage(const ClientMessage& acMessage);

    // Pump helper: calls server.Update() and this->Update() until connected or timeout
    bool WaitForConnection(Server::GameServer& aServer, int aMaxIterations = 200);

    // Pump helper: calls server.Update() and this->Update() for N iterations
    void Pump(Server::GameServer& aServer, int aIterations = 50);

    bool m_connected{false};
    uint32_t m_lastOpcode{0};
    bool m_authAccepted{false};
    AuthenticationResponse::ResponseType m_authResponseType{};
    bool m_gotAuthResponse{false};
    bool m_gotAssignment{false};
    uint32_t m_characterServerId{0};

protected:
    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;
};
