#include "tcp_client.h"

#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

TcpClient::~TcpClient() {
    Disconnect();
}

bool TcpClient::Connect(uint16_t aPort) {
    if (m_socket != -1)
        Disconnect();

    m_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == -1) {
        spdlog::error("socket() failed: {} ({})", strerror(errno), errno);
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(aPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(m_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        spdlog::error("connect(127.0.0.1:{}) failed: {} ({})", aPort, strerror(errno), errno);
        ::close(m_socket);
        m_socket = -1;
        return false;
    }

    m_lineBuffer.Clear();
    spdlog::info("Connected to DLL TCP server at 127.0.0.1:{}", aPort);
    return true;
}

void TcpClient::Disconnect() {
    if (m_socket != -1) {
        ::close(m_socket);
        m_socket = -1;
        m_lineBuffer.Clear();
        spdlog::info("Disconnected from DLL TCP server");
    }
}

bool TcpClient::Send(const void* aData, uint32_t aSize) {
    if (m_socket == -1)
        return false;

    const uint8_t* pBuf = static_cast<const uint8_t*>(aData);
    uint32_t remaining = aSize;

    while (remaining > 0) {
        ssize_t sent = ::send(m_socket, pBuf, remaining, MSG_NOSIGNAL);
        if (sent <= 0) {
            spdlog::error("send() failed: {} ({})", strerror(errno), errno);
            return false;
        }
        pBuf += sent;
        remaining -= static_cast<uint32_t>(sent);
    }
    return true;
}

bool TcpClient::SendJsonLine(const nlohmann::json& aMsg) {
    std::string line = aMsg.dump() + "\n";
    return Send(line.c_str(), static_cast<uint32_t>(line.size()));
}

bool TcpClient::ReceiveJsonLine(nlohmann::json& aOut) {
    while (!m_lineBuffer.HasCompleteLine()) {
        char buf[4096];
        ssize_t n = ::recv(m_socket, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        m_lineBuffer.Append(buf, static_cast<size_t>(n));
    }
    std::string line;
    if (!m_lineBuffer.PopLine(line)) return false;
    aOut = nlohmann::json::parse(line, nullptr, false);
    if (aOut.is_discarded()) {
        spdlog::warn("Malformed JSON from DLL: {}", line.substr(0, 80));
        return true; // not a disconnect, just bad data
    }
    return true;
}

bool TcpClient::SendControlJson(const char* aOp) {
    nlohmann::json msg = {{JsonField::kOp, aOp}};
    return SendJsonLine(msg);
}

bool TcpClient::ReadExact(void* aBuf, size_t aLen) {
    uint8_t* pBuf = static_cast<uint8_t*>(aBuf);
    size_t remaining = aLen;

    while (remaining > 0) {
        ssize_t received = ::recv(m_socket, pBuf, remaining, 0);
        if (received <= 0) {
            if (received == 0) {
                spdlog::warn("TCP connection closed by peer");
            } else {
                spdlog::error("recv() failed: {} ({})", strerror(errno), errno);
            }
            return false;
        }
        pBuf += received;
        remaining -= static_cast<size_t>(received);
    }
    return true;
}
