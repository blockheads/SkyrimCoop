#pragma once

#include <nlohmann/json.hpp>
#include <Messages/Message.h>
#include <cstdint>
#include <string_view>
#include <vector>

struct MessageSerializer
{
    // Serialize any ClientMessage to JSON:
    // {"type":"AuthenticationRequest","opcode":0,"payload":"base64..."}
    static nlohmann::json ClientToJson(const ClientMessage& acMessage);

    // Serialize any ServerMessage to JSON:
    // {"type":"AuthenticationResponse","opcode":0,"payload":"base64..."}
    static nlohmann::json ServerToJson(const ServerMessage& acMessage);

    // Deserialize JSON back to binary buffer (opcode byte + serialized raw payload).
    // Suitable for feeding into factory Extract() which reads opcode then DeserializeRaw().
    static std::vector<uint8_t> ClientFromJson(const nlohmann::json& acJson);
    static std::vector<uint8_t> ServerFromJson(const nlohmann::json& acJson);

    // Type name lookups
    static std::string_view GetClientMessageName(ClientOpcode aOpcode);
    static std::string_view GetServerMessageName(ServerOpcode aOpcode);
    static ClientOpcode GetClientOpcodeByName(std::string_view aName);
    static ServerOpcode GetServerOpcodeByName(std::string_view aName);
};
