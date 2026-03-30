#include <glm/glm.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include "Recording/MessageSerializer.h"
#include "Recording/Base64.h"

#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/WorldStateSync.h>
#include <Opcodes.h>

#include <array>
#include <cstring>

// -- Name trait: specialized per message type via macro --

template <class T>
struct MessageName;

// Client message name registrations (all 47 opcodes, 0..kClientOpcodeMax-1)
#define DEF_MSG_NAME(T) \
    template <> struct MessageName<T> { static constexpr const char* value = #T; };

DEF_MSG_NAME(AuthenticationRequest)
DEF_MSG_NAME(CancelAssignmentRequest)
DEF_MSG_NAME(AssignCharacterRequest)
DEF_MSG_NAME(ClientReferencesMoveRequest)
DEF_MSG_NAME(RequestInventoryChanges)
DEF_MSG_NAME(RequestFactionsChanges)
DEF_MSG_NAME(RequestQuestUpdate)
DEF_MSG_NAME(PartyInviteRequest)
DEF_MSG_NAME(PartyAcceptInviteRequest)
DEF_MSG_NAME(PartyLeaveRequest)
DEF_MSG_NAME(PartyCreateRequest)
DEF_MSG_NAME(PartyChangeLeaderRequest)
DEF_MSG_NAME(PartyKickRequest)
DEF_MSG_NAME(RequestActorValueChanges)
DEF_MSG_NAME(RequestActorMaxValueChanges)
DEF_MSG_NAME(RequestHealthChangeBroadcast)
DEF_MSG_NAME(ActivateRequest)
DEF_MSG_NAME(LockChangeRequest)
DEF_MSG_NAME(AssignObjectsRequest)
DEF_MSG_NAME(RequestDeathStateChange)
DEF_MSG_NAME(RequestObjectInventoryChanges)
DEF_MSG_NAME(SpellCastRequest)
DEF_MSG_NAME(InterruptCastRequest)
DEF_MSG_NAME(AddTargetRequest)
DEF_MSG_NAME(RemoveSpellRequest)
DEF_MSG_NAME(ProjectileLaunchRequest)
DEF_MSG_NAME(ScriptAnimationRequest)
DEF_MSG_NAME(DrawWeaponRequest)
DEF_MSG_NAME(MountRequest)
DEF_MSG_NAME(NewPackageRequest)
DEF_MSG_NAME(RequestRespawn)
DEF_MSG_NAME(SyncExperienceRequest)
DEF_MSG_NAME(RequestEquipmentChanges)
DEF_MSG_NAME(SendChatMessageRequest)
DEF_MSG_NAME(TeleportCommandRequest)
DEF_MSG_NAME(PlayerRespawnRequest)
DEF_MSG_NAME(DialogueRequest)
DEF_MSG_NAME(SubtitleRequest)
DEF_MSG_NAME(PlayerDialogueRequest)
DEF_MSG_NAME(PlayerLevelRequest)
DEF_MSG_NAME(TeleportRequest)
DEF_MSG_NAME(RequestPlayerHealthUpdate)
DEF_MSG_NAME(RequestWeatherChange)
DEF_MSG_NAME(RequestCurrentWeather)
DEF_MSG_NAME(RequestSetWaypoint)
DEF_MSG_NAME(RequestRemoveWaypoint)
DEF_MSG_NAME(SetTimeCommandRequest)

// Server message name registrations (all 53 opcodes, 0..kServerOpcodeMax-1)
DEF_MSG_NAME(AuthenticationResponse)
DEF_MSG_NAME(AssignCharacterResponse)
DEF_MSG_NAME(ServerReferencesMoveRequest)
DEF_MSG_NAME(ServerTimeSettings)
DEF_MSG_NAME(CharacterSpawnRequest)
DEF_MSG_NAME(NotifyInventoryChanges)
DEF_MSG_NAME(StringCacheUpdate)
DEF_MSG_NAME(NotifyFactionsChanges)
DEF_MSG_NAME(NotifyRemoveCharacter)
DEF_MSG_NAME(NotifyQuestUpdate)
DEF_MSG_NAME(NotifyPlayerList)
DEF_MSG_NAME(NotifyPartyInfo)
DEF_MSG_NAME(NotifyPartyInvite)
DEF_MSG_NAME(NotifyPartyJoined)
DEF_MSG_NAME(NotifyPartyLeft)
DEF_MSG_NAME(NotifyActorValueChanges)
DEF_MSG_NAME(NotifyActorMaxValueChanges)
DEF_MSG_NAME(NotifyHealthChangeBroadcast)
DEF_MSG_NAME(NotifySpawnData)
DEF_MSG_NAME(NotifyActivate)
DEF_MSG_NAME(NotifyLockChange)
DEF_MSG_NAME(AssignObjectsResponse)
DEF_MSG_NAME(NotifyDeathStateChange)
DEF_MSG_NAME(NotifyObjectInventoryChanges)
DEF_MSG_NAME(NotifySpellCast)
DEF_MSG_NAME(NotifyInterruptCast)
DEF_MSG_NAME(NotifyAddTarget)
DEF_MSG_NAME(NotifyRemoveSpell)
DEF_MSG_NAME(NotifyProjectileLaunch)
DEF_MSG_NAME(NotifyScriptAnimation)
DEF_MSG_NAME(NotifyDrawWeapon)
DEF_MSG_NAME(NotifyMount)
DEF_MSG_NAME(NotifyNewPackage)
DEF_MSG_NAME(NotifyRespawn)
DEF_MSG_NAME(NotifySyncExperience)
DEF_MSG_NAME(NotifyEquipmentChanges)
DEF_MSG_NAME(NotifyChatMessageBroadcast)
DEF_MSG_NAME(TeleportCommandResponse)
DEF_MSG_NAME(NotifyPlayerRespawn)
DEF_MSG_NAME(NotifyDialogue)
DEF_MSG_NAME(NotifySubtitle)
DEF_MSG_NAME(NotifyPlayerDialogue)
DEF_MSG_NAME(NotifyActorTeleport)
DEF_MSG_NAME(NotifyRelinquishControl)
DEF_MSG_NAME(NotifyPlayerLeft)
DEF_MSG_NAME(NotifyPlayerJoined)
DEF_MSG_NAME(NotifyPlayerLevel)
DEF_MSG_NAME(NotifyTeleport)
DEF_MSG_NAME(NotifyPlayerHealthUpdate)
DEF_MSG_NAME(NotifySettingsChange)
DEF_MSG_NAME(NotifyWeatherChange)
DEF_MSG_NAME(NotifySetWaypoint)
DEF_MSG_NAME(NotifyRemoveWaypoint)
DEF_MSG_NAME(NotifySetTimeResult)
DEF_MSG_NAME(WorldStateSync)

#undef DEF_MSG_NAME

// -- Static name arrays built via Visit() at static init time --

static std::array<const char*, kClientOpcodeMax> s_clientNames{};
static std::array<const char*, kServerOpcodeMax> s_serverNames{};

namespace
{

struct NameArrayInit
{
    NameArrayInit()
    {
        s_clientNames.fill(nullptr);
        s_serverNames.fill(nullptr);

        ClientMessageFactory::Visit([](auto meta) {
            using MsgType = typename decltype(meta)::Type;
            s_clientNames[static_cast<size_t>(MsgType::Opcode)] = MessageName<MsgType>::value;
            return false; // continue visiting
        });

        ServerMessageFactory::Visit([](auto meta) {
            using MsgType = typename decltype(meta)::Type;
            s_serverNames[static_cast<size_t>(MsgType::Opcode)] = MessageName<MsgType>::value;
            return false; // continue visiting
        });
    }
} s_nameArrayInit;

} // anonymous namespace

// -- Serialization helpers --

static std::vector<uint8_t> SerializeMessageRaw(const ClientMessage& acMessage)
{
    // Serialize: writes opcode byte (via base SerializeRaw) + derived SerializeRaw + SerializeDifferential
    TiltedPhoques::Buffer buf(4096);
    TiltedPhoques::Buffer::Writer writer(&buf);
    acMessage.Serialize(writer);

    const size_t byteCount = (writer.GetBitPosition() + 7) / 8;
    return std::vector<uint8_t>(buf.GetData(), buf.GetData() + byteCount);
}

static std::vector<uint8_t> SerializeMessageRaw(const ServerMessage& acMessage)
{
    TiltedPhoques::Buffer buf(4096);
    TiltedPhoques::Buffer::Writer writer(&buf);
    acMessage.Serialize(writer);

    const size_t byteCount = (writer.GetBitPosition() + 7) / 8;
    return std::vector<uint8_t>(buf.GetData(), buf.GetData() + byteCount);
}

// -- Public API --

nlohmann::json MessageSerializer::ClientToJson(const ClientMessage& acMessage)
{
    const auto opcode = acMessage.GetOpcode();
    auto binary = SerializeMessageRaw(acMessage);

    // binary[0] is the opcode byte written by base class SerializeRaw.
    // Payload is everything after the opcode byte.
    const uint8_t* pPayload = binary.data() + 1;
    const size_t payloadSize = binary.size() > 1 ? binary.size() - 1 : 0;

    nlohmann::json j;
    j["type"] = GetClientMessageName(opcode);
    j["opcode"] = static_cast<int>(opcode);
    j["payload"] = Recording::Base64Encode(pPayload, payloadSize);
    return j;
}

nlohmann::json MessageSerializer::ServerToJson(const ServerMessage& acMessage)
{
    const auto opcode = acMessage.GetOpcode();
    auto binary = SerializeMessageRaw(acMessage);

    const uint8_t* pPayload = binary.data() + 1;
    const size_t payloadSize = binary.size() > 1 ? binary.size() - 1 : 0;

    nlohmann::json j;
    j["type"] = GetServerMessageName(opcode);
    j["opcode"] = static_cast<int>(opcode);
    j["payload"] = Recording::Base64Encode(pPayload, payloadSize);
    return j;
}

std::vector<uint8_t> MessageSerializer::ClientFromJson(const nlohmann::json& acJson)
{
    const auto opcode = static_cast<uint8_t>(acJson.at("opcode").get<int>());
    const auto payload = Recording::Base64Decode(acJson.at("payload").get<std::string>());

    // Format: [opcode_byte][raw_payload] -- matches what Extract() expects
    std::vector<uint8_t> result;
    result.reserve(1 + payload.size());
    result.push_back(opcode);
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<uint8_t> MessageSerializer::ServerFromJson(const nlohmann::json& acJson)
{
    const auto opcode = static_cast<uint8_t>(acJson.at("opcode").get<int>());
    const auto payload = Recording::Base64Decode(acJson.at("payload").get<std::string>());

    std::vector<uint8_t> result;
    result.reserve(1 + payload.size());
    result.push_back(opcode);
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::string_view MessageSerializer::GetClientMessageName(ClientOpcode aOpcode)
{
    const auto idx = static_cast<size_t>(aOpcode);
    if (idx < s_clientNames.size() && s_clientNames[idx] != nullptr)
        return s_clientNames[idx];
    return "";
}

std::string_view MessageSerializer::GetServerMessageName(ServerOpcode aOpcode)
{
    const auto idx = static_cast<size_t>(aOpcode);
    if (idx < s_serverNames.size() && s_serverNames[idx] != nullptr)
        return s_serverNames[idx];
    return "";
}

ClientOpcode MessageSerializer::GetClientOpcodeByName(std::string_view aName)
{
    for (size_t i = 0; i < s_clientNames.size(); ++i)
    {
        if (s_clientNames[i] != nullptr && aName == s_clientNames[i])
            return static_cast<ClientOpcode>(i);
    }
    return kClientOpcodeMax;
}

ServerOpcode MessageSerializer::GetServerOpcodeByName(std::string_view aName)
{
    for (size_t i = 0; i < s_serverNames.size(); ++i)
    {
        if (s_serverNames[i] != nullptr && aName == s_serverNames[i])
            return static_cast<ServerOpcode>(i);
    }
    return kServerOpcodeMax;
}
