#include <catch2/catch.hpp>
#include <cstring>
#include <set>

#include "protocol.h"

TEST_CASE("RelayProtocol_HookEventPacket_RoundTrip", "[RelayProtocol]")
{
    HookEventPacket original{};
    original.header.opcode = HOOK_DAMAGE_ACTOR;
    original.header.length = sizeof(HookEventPacket) - sizeof(PacketHeader);
    original.argCount = 3;
    original.args[0] = 0xDEADBEEF12345678ULL;
    original.args[1] = 0x0000000100000002ULL;
    original.args[2] = 0xFFFFFFFFFFFFFFFFULL;

    // Round-trip through memcpy
    uint8_t buffer[sizeof(HookEventPacket)];
    std::memcpy(buffer, &original, sizeof(HookEventPacket));

    HookEventPacket restored{};
    std::memcpy(&restored, buffer, sizeof(HookEventPacket));

    REQUIRE(restored.header.opcode == HOOK_DAMAGE_ACTOR);
    REQUIRE(restored.header.length == original.header.length);
    REQUIRE(restored.argCount == 3);
    REQUIRE(restored.args[0] == 0xDEADBEEF12345678ULL);
    REQUIRE(restored.args[1] == 0x0000000100000002ULL);
    REQUIRE(restored.args[2] == 0xFFFFFFFFFFFFFFFFULL);
}

TEST_CASE("RelayProtocol_HookEventPacket_All8Args", "[RelayProtocol]")
{
    HookEventPacket pkt{};
    pkt.header.opcode = HOOK_SET_POSITION;
    pkt.argCount = 8;
    for (uint8_t i = 0; i < 8; i++)
        pkt.args[i] = static_cast<uint64_t>(i) * 0x1111111111111111ULL;

    uint8_t buffer[sizeof(HookEventPacket)];
    std::memcpy(buffer, &pkt, sizeof(HookEventPacket));

    HookEventPacket restored{};
    std::memcpy(&restored, buffer, sizeof(HookEventPacket));

    REQUIRE(restored.argCount == 8);
    for (uint8_t i = 0; i < 8; i++)
        REQUIRE(restored.args[i] == static_cast<uint64_t>(i) * 0x1111111111111111ULL);
}

TEST_CASE("RelayProtocol_CommandPacket_RoundTrip", "[RelayProtocol]")
{
    CommandPacket original{};
    original.header.opcode = CMD_SET_POSITION;
    original.header.length = sizeof(CommandPacket) - sizeof(PacketHeader);
    original.argCount = 4;
    original.args[0] = 0x00000001ULL;
    original.args[1] = 0x4048F5C3ULL;  // float-as-bits example
    original.args[2] = 0x40590000ULL;
    original.args[3] = 0x40690000ULL;

    uint8_t buffer[sizeof(CommandPacket)];
    std::memcpy(buffer, &original, sizeof(CommandPacket));

    CommandPacket restored{};
    std::memcpy(&restored, buffer, sizeof(CommandPacket));

    REQUIRE(restored.header.opcode == CMD_SET_POSITION);
    REQUIRE(restored.argCount == 4);
    REQUIRE(restored.args[0] == 0x00000001ULL);
    REQUIRE(restored.args[1] == 0x4048F5C3ULL);
    REQUIRE(restored.args[2] == 0x40590000ULL);
    REQUIRE(restored.args[3] == 0x40690000ULL);
}

TEST_CASE("RelayProtocol_PacketTotalSize", "[RelayProtocol]")
{
    PacketHeader h{};
    h.opcode = HOOK_ACTOR_ADDED;
    h.length = 48;

    REQUIRE(PacketTotalSize(h) == sizeof(PacketHeader) + 48);
    REQUIRE(PacketTotalSize(h) == 52);  // sizeof(PacketHeader) == 4
}

TEST_CASE("RelayProtocol_AllHookOpcodes_Distinct", "[RelayProtocol]")
{
    std::set<uint16_t> opcodes;
    uint16_t hookValues[] = {
        HOOK_ACTOR_ADDED, HOOK_ACTOR_REMOVED, HOOK_DAMAGE_ACTOR,
        HOOK_SET_POSITION, HOOK_SPELL_CAST, HOOK_ADD_INVENTORY,
        HOOK_EQUIP, HOOK_WEATHER_CHANGE, HOOK_UNEQUIP,
        HOOK_ACTIVATE, HOOK_LOCK_CHANGE, HOOK_PROJECTILE,
        HOOK_ACTOR_PROCESS, HOOK_CHARACTER_CTOR, HOOK_SPAWN_ACTOR,
        HOOK_DEATH_ITEMS, HOOK_REGEN_ATTR, HOOK_APPLY_EFFECT,
        HOOK_PERFORM_ACTION, HOOK_REMOVE_SPELL, HOOK_PICK_UP,
        HOOK_DROP_OBJECT, HOOK_ADD_TARGET, HOOK_INTERRUPT_CAST,
        HOOK_SIMULATE_TIME, HOOK_SET_WEATHER, HOOK_FORCE_WEATHER,
        HOOK_REMOVE_INV_ITEM, HOOK_PLAY_ANIMATION, HOOK_MOUNT_PACKAGE,
    };
    constexpr size_t count = sizeof(hookValues) / sizeof(hookValues[0]);

    for (size_t i = 0; i < count; i++)
        opcodes.insert(hookValues[i]);

    REQUIRE(opcodes.size() == count);
    REQUIRE(count >= 20);  // at least 20 hook opcodes
}

TEST_CASE("RelayProtocol_AllCommandOpcodes_Distinct", "[RelayProtocol]")
{
    std::set<uint16_t> opcodes;
    uint16_t cmdValues[] = {
        CMD_SET_POSITION, CMD_FORCE_WEATHER, CMD_CAST_SPELL,
        CMD_EQUIP_ITEM, CMD_UNEQUIP_ITEM, CMD_PLAY_ANIMATION,
        CMD_SET_ACTOR_VALUE, CMD_DAMAGE_ACTOR, CMD_ADD_SPELL,
        CMD_REMOVE_SPELL, CMD_ADD_ITEM, CMD_REMOVE_ITEM,
        CMD_ACTIVATE, CMD_LOCK_CHANGE, CMD_LAUNCH_PROJECTILE,
    };
    constexpr size_t count = sizeof(cmdValues) / sizeof(cmdValues[0]);

    for (size_t i = 0; i < count; i++)
        opcodes.insert(cmdValues[i]);

    REQUIRE(opcodes.size() == count);
    REQUIRE(count >= 10);  // at least 10 command opcodes
}
