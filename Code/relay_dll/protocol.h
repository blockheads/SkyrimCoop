#pragma once
#include <cstdint>

// All packets: opcode(u16) + length(u16) + payload(variable)
struct PacketHeader {
    uint16_t opcode;
    uint16_t length;  // payload length excluding header
};

// Hook events (DLL -> Native): fixed-size, up to 8 raw uint64 args
enum HookOpcode : uint16_t {
    HOOK_ACTOR_ADDED      = 0x0001,
    HOOK_ACTOR_REMOVED    = 0x0002,
    HOOK_DAMAGE_ACTOR     = 0x0003,
    HOOK_SET_POSITION     = 0x0004,
    HOOK_SPELL_CAST       = 0x0005,
    HOOK_ADD_INVENTORY    = 0x0006,
    HOOK_EQUIP            = 0x0007,
    HOOK_WEATHER_CHANGE   = 0x0008,
    HOOK_UNEQUIP          = 0x0009,
    HOOK_ACTIVATE         = 0x000A,
    HOOK_LOCK_CHANGE      = 0x000B,
    HOOK_PROJECTILE       = 0x000C,
    HOOK_ACTOR_PROCESS    = 0x000D,
    HOOK_CHARACTER_CTOR   = 0x000E,
    HOOK_SPAWN_ACTOR      = 0x000F,
    HOOK_DEATH_ITEMS      = 0x0010,
    HOOK_REGEN_ATTR       = 0x0011,
    HOOK_APPLY_EFFECT     = 0x0012,
    HOOK_PERFORM_ACTION   = 0x0013,
    HOOK_REMOVE_SPELL     = 0x0014,
    HOOK_PICK_UP          = 0x0015,
    HOOK_DROP_OBJECT      = 0x0016,
    HOOK_ADD_TARGET       = 0x0017,
    HOOK_INTERRUPT_CAST   = 0x0018,
    HOOK_SIMULATE_TIME    = 0x0019,
    HOOK_SET_WEATHER      = 0x001A,
    HOOK_FORCE_WEATHER    = 0x001B,
    HOOK_REMOVE_INV_ITEM  = 0x001C,
    HOOK_PLAY_ANIMATION   = 0x001D,
    HOOK_MOUNT_PACKAGE    = 0x001E,
};

struct HookEventPacket {
    PacketHeader header;
    uint8_t argCount;
    uint8_t _pad[7];     // align args to 8-byte boundary
    uint64_t args[8];
};

// Commands (Native -> DLL): request to execute game function on game thread
enum CommandOpcode : uint16_t {
    CMD_SET_POSITION      = 0x8001,
    CMD_FORCE_WEATHER     = 0x8002,
    CMD_CAST_SPELL        = 0x8003,
    CMD_EQUIP_ITEM        = 0x8004,
    CMD_UNEQUIP_ITEM      = 0x8005,
    CMD_PLAY_ANIMATION    = 0x8006,
    CMD_SET_ACTOR_VALUE   = 0x8007,
    CMD_DAMAGE_ACTOR      = 0x8008,
    CMD_ADD_SPELL         = 0x8009,
    CMD_REMOVE_SPELL      = 0x800A,
    CMD_ADD_ITEM          = 0x800B,
    CMD_REMOVE_ITEM       = 0x800C,
    CMD_ACTIVATE          = 0x800D,
    CMD_LOCK_CHANGE       = 0x800E,
    CMD_LAUNCH_PROJECTILE = 0x800F,
};

// Control messages (bidirectional)
enum ControlOpcode : uint16_t {
    CTRL_HEARTBEAT = 0xF001,
    CTRL_SHUTDOWN  = 0xF002,
    CTRL_READY     = 0xF003,
};

struct CommandPacket {
    PacketHeader header;
    uint8_t argCount;
    uint8_t _pad[7];
    uint64_t args[8];
};

struct ControlPacket {
    PacketHeader header;
};

// Helper: compute total packet size from header
inline uint32_t PacketTotalSize(const PacketHeader& h) {
    return sizeof(PacketHeader) + h.length;
}
