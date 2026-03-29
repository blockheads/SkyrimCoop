# Phase 11: JSON Wire Protocol - Research

**Researched:** 2026-03-29
**Domain:** C++ IPC protocol replacement (binary structs to NDJSON), dirty-check caching
**Confidence:** HIGH

## Summary

This phase replaces the flat binary TCP protocol between the relay DLL (MinGW, runs inside Skyrim under Wine) and the native Linux client with newline-delimited JSON (NDJSON). Each of the ~31 hook trampolines in `hook_trampolines.cpp` currently packs raw `uint64_t args[8]` into a `HookEventPacket` struct. These become JSON objects with named, typed fields. Similarly, commands (native -> DLL) switch from `CommandPacket` binary to JSON. A dirty-check cache is added DLL-side so high-frequency hooks (HOOK_ACTOR_PROCESS fires per-actor per-frame, HOOK_SET_POSITION fires per-ref per-frame) only send when values actually change.

The scope is well-bounded: two send paths (DLL `TcpServer::Send`, native `TcpClient::Send`), two receive paths (DLL `TcpServer::Receive`/`DrainCommandQueue`, native `TcpClient::Receive`), 31 hook trampolines, 8 service `OnHookEvent()` methods, and the main loop dispatch in `main.cpp`. nlohmann/json is the standard single-header C++ JSON library and works with both MinGW (DLL) and GCC (native).

**Primary recommendation:** Use nlohmann/json v3.11.3+ (single header) on both sides. Frame with NDJSON (one JSON object per `\n`). Implement dirty-check cache as `std::unordered_map<uint32_t, CachedState>` per high-frequency opcode in the DLL. Convert all 31 trampolines, all 15 command senders, and all 8 service receivers.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Use nlohmann/json (single-header, header-only C++). Drop `nlohmann/json.hpp` into the relay DLL source tree. No build system changes needed -- just `#include`.
- **D-02:** Native client side also uses nlohmann/json for parsing incoming hook events and serializing outgoing commands.
- **D-03:** Typed fields per opcode. Each hook event and command has named fields matching its semantics, not generic `args[]` arrays. Self-documenting and debuggable with tools like `jq`.
- **D-04:** Hook events (DLL -> Native) use lowercase snake_case op names: `"op":"set_position"`, `"op":"actor_added"`, etc.
- **D-05:** Commands (Native -> DLL) use `cmd_` prefix: `"op":"cmd_set_position"`, `"op":"cmd_cast_spell"`, etc.
- **D-06:** Control messages use `ctrl_` prefix: `"op":"ctrl_heartbeat"`, `"op":"ctrl_shutdown"`, `"op":"ctrl_ready"`.
- **D-07:** Newline-delimited JSON (NDJSON) -- one JSON object per line, `\n` terminated. Simple framing, no length prefix needed.
- **D-08:** Big bang cutover. Both DLL and native client switch from binary to JSON in this phase. No version negotiation, no parallel support.
- **D-09:** `protocol.h` kept as reference for opcode documentation. New `json_protocol.h` (or equivalent) defines the JSON field names/schema.
- **D-10:** JSON serialization overhead is not a concern for localhost IPC.
- **D-11:** Add a dirty-check cache in the DLL. Track last-sent state per actor/entity. Only serialize and send a hook event when values have actually changed since the last send.
- **D-12:** Cache granularity is per-opcode-per-entity. For position hooks: cache last (x, y, z, rx, ry, rz) per formId. For actor process: cache last delta or state per formId. Only send when diff exceeds a threshold (e.g., position moved more than epsilon).

### Claude's Discretion
- Exact dirty-check thresholds (position epsilon, timing for non-spatial hooks)
- Whether to use a hash-based or value-comparison approach for the cache
- Internal data structure for the per-entity cache (flat array, unordered_map, etc.)
- Whether to batch multiple dirty events into a single NDJSON burst or send individually
- Exact JSON field names for each of the ~30 hook opcodes and ~15 command opcodes
- How to handle the HOOK_PROJECTILE special case (no formId, raw data pointer)
- Whether mock_skyrim_loader and fake_player JSON updates are in-scope or deferred

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| nlohmann/json | 3.11.3 | JSON serialization/deserialization | De facto C++ JSON standard, single-header, MIT license, works with GCC/MinGW/MSVC, C++11+ |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| (none) | - | - | nlohmann/json is the only new dependency |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| nlohmann/json | RapidJSON | Faster but more complex API; D-01 locks nlohmann |
| nlohmann/json | simdjson | Read-only parser, no serialization; doesn't fit bidirectional protocol |

**Installation:**
```bash
# Download single header into relay DLL source tree
curl -L -o Code/relay_dll/nlohmann/json.hpp \
  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

# Also place in native_client (or use shared include path)
mkdir -p Code/native_client/third_party/nlohmann
cp Code/relay_dll/nlohmann/json.hpp Code/native_client/third_party/nlohmann/
```

**Version verification:** v3.11.3 is the latest stable release. The library is header-only, so no linking or build system changes are needed -- just `#include "nlohmann/json.hpp"`.

**MinGW compatibility note (HIGH confidence):** nlohmann/json is tested on GCC 7+ and works with MinGW. C++20 comparison operator issues were fixed in v3.11.x. Use v3.11.3 to avoid known issues.

## Architecture Patterns

### Recommended Project Structure
```
Code/
├── relay_dll/
│   ├── nlohmann/json.hpp          # Single-header JSON lib (new)
│   ├── json_protocol.h            # JSON field name constants, builder helpers (new)
│   ├── dirty_cache.h              # Per-entity dirty-check cache (new)
│   ├── hook_trampolines.cpp       # Modified: build JSON objects instead of HookEventPacket
│   ├── tcp_server.cpp             # Modified: send NDJSON lines instead of binary structs
│   ├── tcp_server.h               # Modified: SendJson(const nlohmann::json&), ReceiveJsonLine()
│   ├── relay_main.cpp             # Modified: DrainCommandQueue parses JSON commands
│   └── protocol.h                 # Kept as reference (opcode docs), no longer used for wire format
├── native_client/
│   ├── third_party/nlohmann/json.hpp  # Same header (or shared path)
│   ├── game_bridge/
│   │   ├── tcp_client.cpp         # Modified: ReceiveJsonLine(), SendJsonLine()
│   │   └── tcp_client.h           # Modified: new JSON-based API
│   ├── Services/
│   │   ├── CharacterService.cpp   # Modified: OnHookEvent takes json, SendCommand builds json
│   │   ├── [all 8 services].cpp   # Modified: same pattern
│   │   └── ...
│   └── main.cpp                   # Modified: dispatch on json["op"] string
```

### Pattern 1: NDJSON Line Framing
**What:** Each message is a single JSON object serialized to one line, terminated by `\n`. The receiver reads until `\n` and parses that line as JSON.
**When to use:** All messages between DLL and native client.
**Example:**
```cpp
// DLL side: sending a hook event
void TcpServer::SendJson(const nlohmann::json& aMsg)
{
    std::string line = aMsg.dump() + "\n";
    Send(line.c_str(), static_cast<uint32_t>(line.size()));
}

// Native side: receiving a line
bool TcpClient::ReceiveJsonLine(nlohmann::json& aOut)
{
    // Read bytes into line buffer until '\n'
    // Parse: aOut = nlohmann::json::parse(line);
}
```

### Pattern 2: Line Buffer for TCP Stream Reassembly
**What:** TCP is a byte stream, not a message stream. A `\n`-delimited protocol needs a line buffer that handles partial reads. Bytes arrive in arbitrary chunks -- you must buffer until you see `\n`, then parse, and keep leftover bytes for the next message.
**When to use:** Both TcpServer::Receive (DLL side) and TcpClient::Receive (native side).
**Example:**
```cpp
class LineBuffer {
public:
    // Append raw bytes from recv(). Returns number of complete lines available.
    int Append(const char* aData, size_t aLen);

    // Pop the next complete line (without trailing \n). Returns false if none.
    bool PopLine(std::string& aOut);

private:
    std::string m_buffer;
};
```
**Critical detail:** The existing `RecvExact()` pattern (read N bytes) does not work for NDJSON. You must switch to buffered line reading. This is the most important implementation change in the TCP layer.

### Pattern 3: Dirty-Check Cache (DLL Side)
**What:** Track last-sent state per entity per opcode. Only serialize and send when values differ beyond a threshold.
**When to use:** High-frequency hooks: HOOK_SET_POSITION, HOOK_ACTOR_PROCESS, HOOK_SPAWN_ACTOR.
**Example:**
```cpp
// dirty_cache.h
struct PositionState {
    float x, y, z, rx, ry, rz;
};

class DirtyCache {
public:
    // Returns true if this position is dirty (changed enough to send)
    bool CheckPosition(uint32_t aFormId, float aX, float aY, float aZ,
                       float aRx, float aRy, float aRz);

    // Remove entry when actor is removed
    void Remove(uint32_t aFormId);
    void Clear();

private:
    std::unordered_map<uint32_t, PositionState> m_positions;
    static constexpr float kPositionEpsilon = 1.0f;      // units
    static constexpr float kRotationEpsilon = 0.01f;      // radians
};
```

### Pattern 4: JSON Message Schema (json_protocol.h)
**What:** String constants for all JSON field names and op names, plus builder helpers.
**When to use:** Both DLL and native client include this header for consistent field names.
**Example:**
```cpp
// json_protocol.h
#pragma once
#include "nlohmann/json.hpp"

namespace JsonOp {
    // Hook events (DLL -> Native)
    constexpr const char* kActorAdded     = "actor_added";
    constexpr const char* kActorRemoved   = "actor_removed";
    constexpr const char* kSetPosition    = "set_position";
    constexpr const char* kActorProcess   = "actor_process";
    constexpr const char* kSpawnActor     = "spawn_actor";
    // ... all 30 hook ops

    // Commands (Native -> DLL)
    constexpr const char* kCmdSetPosition = "cmd_set_position";
    constexpr const char* kCmdCastSpell   = "cmd_cast_spell";
    // ... all 15 command ops

    // Control
    constexpr const char* kCtrlHeartbeat  = "ctrl_heartbeat";
    constexpr const char* kCtrlShutdown   = "ctrl_shutdown";
    constexpr const char* kCtrlReady      = "ctrl_ready";
}

namespace JsonField {
    constexpr const char* kOp        = "op";
    constexpr const char* kFormId    = "form_id";
    constexpr const char* kActorPtr  = "actor_ptr";
    constexpr const char* kPosX      = "x";
    constexpr const char* kPosY      = "y";
    constexpr const char* kPosZ      = "z";
    constexpr const char* kRotX      = "rx";
    constexpr const char* kRotY      = "ry";
    constexpr const char* kRotZ      = "rz";
    constexpr const char* kDamage    = "damage";
    constexpr const char* kItemFormId = "item_form_id";
    constexpr const char* kCount     = "count";
    constexpr const char* kSpellFormId = "spell_form_id";
    // ... etc
}
```

### Pattern 5: Service Interface Migration
**What:** Change all 8 service `OnHookEvent(const HookEventPacket&)` to `OnHookEvent(const nlohmann::json&)` and all command builders from binary to JSON.
**When to use:** Every service in `Code/native_client/Services/`.
**Example:**
```cpp
// Before (binary):
void CharacterService::OnHookEvent(const HookEventPacket& aPkt) {
    switch (static_cast<HookOpcode>(aPkt.header.opcode)) {
    case HOOK_SET_POSITION:
        if (aPkt.argCount >= 8) {
            float px = U64ToFloat(aPkt.args[2]);
            // ...
        }
        break;
    }
}

// After (JSON):
void CharacterService::OnHookEvent(const nlohmann::json& aMsg) {
    const auto& op = aMsg["op"].get<std::string>();
    if (op == JsonOp::kSetPosition) {
        uint32_t formId = aMsg["form_id"].get<uint32_t>();
        float px = aMsg["x"].get<float>();
        float py = aMsg["y"].get<float>();
        float pz = aMsg["z"].get<float>();
        // ... no U64ToFloat conversion needed!
    }
}
```

### Anti-Patterns to Avoid
- **Keeping binary PacketHeader for framing then JSON for payload:** This defeats the purpose. Use pure NDJSON -- the `\n` delimiter IS the framing.
- **Parsing JSON with exceptions in the DLL:** Use `json::parse(str, nullptr, false)` (nothrow) in the DLL since exceptions may behave oddly under Wine. Check `.is_discarded()` on failure.
- **Allocating per-message in hot paths:** nlohmann::json allocates on the heap. For the DLL where hooks fire at 60Hz per actor, pre-build the JSON object and reuse it (modify fields in-place). Or accept the allocation cost since D-10 says this is fine for localhost IPC.
- **Forgetting to handle partial TCP reads:** TCP gives you arbitrary byte chunks. You MUST buffer until `\n`. The most common bug will be assuming one `recv()` gives you one complete JSON line.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing/serialization | Custom parser | nlohmann/json | Edge cases (escaping, unicode, nested objects, number precision) |
| TCP line buffering | Ad-hoc string splitting | Proper LineBuffer class | Partial reads, buffer boundaries, embedded newlines in strings (though JSON strings escape `\n` as `\\n`) |
| Dirty-check with floating point | Exact equality comparison | Epsilon-based threshold comparison | Floating point drift means exact equality never triggers |

**Key insight:** The FloatToU64/U64ToFloat conversion layer that currently causes subtle bugs (struct alignment, endianness) is entirely eliminated by JSON's native float representation. This is the core motivation for the phase.

## Common Pitfalls

### Pitfall 1: TCP Stream Boundary Mismatch
**What goes wrong:** Assuming one `recv()` call returns exactly one JSON line. In reality, a single `recv()` may return half a line, two lines, or one and a half lines.
**Why it happens:** TCP is a byte stream, not a message stream. The OS kernel coalesces and fragments packets arbitrarily.
**How to avoid:** Implement a `LineBuffer` that accumulates bytes and emits complete lines. Both DLL and native sides need this.
**Warning signs:** Messages parse correctly in testing but fail intermittently under load when TCP segments span line boundaries.

### Pitfall 2: nlohmann/json Exception Safety Under Wine
**What goes wrong:** `json::parse()` throws `json::parse_error` on malformed input. Exceptions in MinGW DLL code running under Wine can crash.
**Why it happens:** C++ exception handling ABI differences between MinGW and Wine runtime.
**How to avoid:** Always use `json::parse(str, nullptr, false)` (nothrow mode) on the DLL side. Check `result.is_discarded()` instead of catching exceptions.
**Warning signs:** Crashes on malformed input that don't produce exception messages.

### Pitfall 3: Dirty Cache Stale Entries
**What goes wrong:** Actors are removed from the game but their cache entries persist, wasting memory or (worse) suppressing events for a new actor that reuses the same formId.
**Why it happens:** HOOK_ACTOR_REMOVED fires but the cache cleanup is missed.
**How to avoid:** The dirty cache MUST clear entries on HOOK_ACTOR_REMOVED. Wire this into the same trampoline that sends the removal event.
**Warning signs:** After long play sessions, cache grows unbounded. Actors that despawn and respawn show stale positions.

### Pitfall 4: Compile Time Explosion from nlohmann/json in DLL
**What goes wrong:** Including `json.hpp` (25K+ lines) in every trampoline function dramatically increases compile time for the DLL, which is currently a fast-compiling ~1700 LOC target.
**Why it happens:** Single-header library is huge. Template-heavy.
**How to avoid:** Include `json.hpp` in ONE translation unit (e.g., `json_protocol.cpp`) and expose thin wrappers. Or accept the compile time hit since the DLL is a single xmake target with few files. Alternatively, use `nlohmann/json_fwd.hpp` (forward declarations) in headers and full include only in `.cpp` files.
**Warning signs:** DLL build time jumps from seconds to 30+ seconds.

### Pitfall 5: Thread Safety on DLL Send Path
**What goes wrong:** Multiple hook trampolines fire concurrently (from different game threads) and race on the JSON serialization + TCP send.
**Why it happens:** The current binary protocol already has this issue and uses `CRITICAL_SECTION m_sendLock`. JSON serialization (string building) must also be inside the lock or use thread-local buffers.
**How to avoid:** Either serialize inside the `CRITICAL_SECTION` (simplest), or serialize to a thread-local string buffer and only lock for the TCP send.
**Warning signs:** Corrupted JSON lines (interleaved bytes from two messages).

### Pitfall 6: String Field Comparison Performance
**What goes wrong:** Dispatching on `json["op"].get<std::string>()` with string comparisons for every hook event seems slow.
**Why it happens:** 8 services each checking string ops on every incoming message.
**How to avoid:** This is actually fine for localhost IPC (D-10). But if desired, dispatch via an `std::unordered_map<std::string, HandlerFn>` built once at startup instead of cascading `if/else`.
**Warning signs:** None in practice for 2-4 player co-op.

## Code Examples

### Example 1: Hook Trampoline Conversion (SetPosition)
```cpp
// Before (binary):
static void __fastcall HookSetPosition(void* apSelf, void* apPos)
{
    float x, y, z;
    ReadPosition(apSelf, &x, &y, &z);
    float rx, ry, rz;
    ReadRotation(apSelf, &rx, &ry, &rz);
    uint32_t formId = ReadFormId(apSelf);

    HookEventPacket pkt{};
    pkt.header.opcode = HOOK_SET_POSITION;
    pkt.header.length = sizeof(HookEventPacket) - sizeof(PacketHeader);
    pkt.argCount = 8;
    pkt.args[0] = reinterpret_cast<uint64_t>(apSelf);
    pkt.args[1] = formId;
    pkt.args[2] = FloatToU64(x);
    // ... etc
    g_tcpServer.Send(&pkt, sizeof(pkt));
    RealSetPosition(apSelf, apPos);
}

// After (JSON + dirty cache):
static void __fastcall HookSetPosition(void* apSelf, void* apPos)
{
    float x, y, z;
    ReadPosition(apSelf, &x, &y, &z);
    float rx, ry, rz;
    ReadRotation(apSelf, &rx, &ry, &rz);
    uint32_t formId = ReadFormId(apSelf);

    if (formId != 0 && g_dirtyCache.CheckPosition(formId, x, y, z, rx, ry, rz))
    {
        nlohmann::json msg = {
            {"op", "set_position"},
            {"form_id", formId},
            {"ptr", reinterpret_cast<uint64_t>(apSelf)},
            {"x", x}, {"y", y}, {"z", z},
            {"rx", rx}, {"ry", ry}, {"rz", rz}
        };
        g_tcpServer.SendJson(msg);
    }
    RealSetPosition(apSelf, apPos);
}
```

### Example 2: NDJSON Line Buffer
```cpp
class LineBuffer {
public:
    void Append(const char* aData, size_t aLen) {
        m_buf.append(aData, aLen);
    }

    bool PopLine(std::string& aOut) {
        auto pos = m_buf.find('\n');
        if (pos == std::string::npos)
            return false;
        aOut = m_buf.substr(0, pos);
        m_buf.erase(0, pos + 1);
        return true;
    }

    bool HasCompleteLine() const {
        return m_buf.find('\n') != std::string::npos;
    }

private:
    std::string m_buf;
};
```

### Example 3: Native Main Loop Dispatch
```cpp
// Before:
if (header.opcode < 0x8000) {
    HookEventPacket hookPkt{};
    hookPkt.header = header;
    memcpy(hookPkt.args, payload + 4, hookPkt.argCount * sizeof(uint64_t));
    DispatchHookEvent(hookPkt, ...);
}

// After:
std::string line;
while (lineBuffer.PopLine(line)) {
    auto msg = nlohmann::json::parse(line, nullptr, false);
    if (msg.is_discarded()) {
        spdlog::warn("Malformed JSON line: {}", line.substr(0, 80));
        continue;
    }
    const auto& op = msg["op"].get_ref<const std::string&>();
    if (op.starts_with("ctrl_")) {
        HandleControlMessage(msg);
    } else if (op.starts_with("cmd_")) {
        spdlog::warn("Unexpected command from DLL: {}", op);
    } else {
        DispatchHookEvent(msg, ...);
    }
}
```

### Example 4: Command Serialization (Native -> DLL)
```cpp
// Before:
CommandPacket cmd{};
cmd.header.opcode = CMD_SET_POSITION;
cmd.header.length = sizeof(CommandPacket) - sizeof(PacketHeader);
cmd.argCount = 3;
cmd.args[0] = reinterpret_cast<uint64_t>(pActor);
cmd.args[1] = (static_cast<uint64_t>(posYBits) << 32) | posXBits;
cmd.args[2] = posZBits;
m_client.Send(&cmd, sizeof(cmd));

// After:
nlohmann::json cmd = {
    {"op", "cmd_set_position"},
    {"actor_ptr", reinterpret_cast<uint64_t>(pActor)},
    {"x", px}, {"y", py}, {"z", pz}
};
m_client.SendJsonLine(cmd);
```

## Complete Hook Event JSON Schema

Based on the arg layout conventions in `protocol.h` lines 52-88, here is the complete schema for all hook events. Each becomes a JSON object with an `"op"` field and named parameters.

### Hook Events (DLL -> Native): 30 ops

| Op Name | Fields | Notes |
|---------|--------|-------|
| `actor_added` | form_id, ptr | |
| `actor_removed` | form_id, ptr | |
| `damage_actor` | form_id, ptr, damage(float) | |
| `set_position` | form_id, ptr, x, y, z, rx, ry, rz | HIGH frequency, dirty-cached |
| `spell_cast` | caster_form_id, caster_ptr, spell_form_id, spell_ptr | |
| `add_inventory` | actor_form_id, actor_ptr, item_form_id, item_ptr, count, ref_ptr | |
| `equip` | actor_form_id, actor_ptr, item_form_id, item_ptr | |
| `weather_change` | sky_ptr | No formId |
| `unequip` | actor_form_id, actor_ptr, item_form_id, item_ptr | |
| `activate` | this_form_id, this_ptr, activator_form_id, activator_ptr, obj_ptr, count | |
| `lock_change` | this_form_id, this_ptr, locked(bool) | |
| `projectile` | data_ptr | Special: no formId, raw launch data |
| `actor_process` | form_id, ptr, delta(float) | HIGH frequency, dirty-cached |
| `character_ctor` | form_id, ptr | formId may be 0 |
| `spawn_actor` | form_id, ptr, x, y, z, rx, ry, rz | |
| `death_items` | form_id, ptr | |
| `regen_attr` | form_id, ptr, type(int), value(float) | |
| `apply_effect` | actor_form_id, actor_ptr, caster_form_id, caster_ptr, effect_ptr | |
| `perform_action` | actor_form_id, actor_ptr, action_form_id, action_ptr, process_ptr | |
| `remove_spell` | actor_form_id, actor_ptr, spell_form_id, spell_ptr | |
| `pick_up` | actor_form_id, actor_ptr, obj_form_id, obj_ptr, count | |
| `drop_object` | actor_form_id, actor_ptr, item_form_id, item_ptr, count | |
| `add_target` | target_form_id, target_ptr, hit_data_ptr | |
| `interrupt_cast` | caster_form_id, caster_ptr, refund(bool) | |
| `simulate_time` | this_ptr | No formId |
| `set_weather` | sky_ptr, weather_form_id, weather_ptr, override(int), accel(float) | |
| `force_weather` | sky_ptr, weather_form_id, weather_ptr, override(int) | |
| `remove_inv_item` | actor_form_id, actor_ptr, item_form_id, item_ptr, count | |
| `play_animation` | this_form_id, this_ptr, anim_ptr | |
| `mount_package` | actor_form_id, actor_ptr, mount_form_id, mount_ptr | |

### Commands (Native -> DLL): 15 ops

| Op Name | Fields |
|---------|--------|
| `cmd_set_position` | actor_ptr, x, y, z |
| `cmd_force_weather` | weather_ptr, override |
| `cmd_cast_spell` | caster_ptr, spell_form_id, target_ptr |
| `cmd_equip_item` | actor_ptr, item_form_id |
| `cmd_unequip_item` | actor_ptr, item_form_id |
| `cmd_play_animation` | actor_ptr, action_id, target_ptr |
| `cmd_set_actor_value` | actor_ptr, av_index, value(float) |
| `cmd_damage_actor` | actor_ptr, damage(float) |
| `cmd_add_spell` | actor_ptr, spell_form_id |
| `cmd_remove_spell` | actor_ptr, spell_form_id |
| `cmd_add_item` | actor_ptr, item_form_id, count |
| `cmd_remove_item` | actor_ptr, item_form_id, count |
| `cmd_activate` | this_ptr, activator_ptr |
| `cmd_lock_change` | this_ptr, locked(bool) |
| `cmd_launch_projectile` | data_ptr |

### Control (Bidirectional): 3 ops

| Op Name | Fields |
|---------|--------|
| `ctrl_heartbeat` | (none) |
| `ctrl_shutdown` | (none) |
| `ctrl_ready` | (none) |

## Dirty-Check Cache Design

### Recommendation: Value-Comparison with Epsilon Thresholds

**Data structure:** `std::unordered_map<uint32_t, CachedState>` where key is formId.

**Thresholds (recommended):**
- Position: 1.0 unit (Skyrim units, ~1.4cm) -- matches existing `kPositionChangeThresholdSq` in CharacterService.cpp
- Rotation: 0.01 radians (~0.57 degrees) -- matches existing `kRotationChangeThreshold`
- Actor process delta: always send (delta changes every frame, but skip if formId has no observer -- deferred optimization)

**Why value-comparison over hashing:** Hashing floats is fragile (NaN, -0.0, precision). Direct value comparison with epsilon is simpler, more predictable, and faster for 6 floats.

**Which hooks to cache:**
| Hook | Cache? | Reason |
|------|--------|--------|
| HOOK_SET_POSITION | YES | Fires per-ref per-frame even when stationary |
| HOOK_ACTOR_PROCESS | YES | Fires per-actor per-frame; only send if delta changed |
| HOOK_SPAWN_ACTOR | NO | One-shot event per spawn |
| All others | NO | Low frequency, event-driven (not polling) |

**Cache lifecycle:**
- Insert/update on HOOK_SET_POSITION/HOOK_ACTOR_PROCESS
- Remove on HOOK_ACTOR_REMOVED
- Clear on CTRL_SHUTDOWN or client disconnect

### Estimated Traffic Reduction

In a typical Skyrim cell with 20 loaded actors at 60fps:
- **Before:** 20 actors * 60fps = 1,200 SET_POSITION messages/sec + 1,200 ACTOR_PROCESS messages/sec = 2,400 msgs/sec
- **After:** Only actors that actually move send updates. Stationary NPCs (majority) produce zero traffic. Estimated 50-200 msgs/sec for typical gameplay.
- **Reduction:** ~90% fewer messages for idle scenes.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Binary struct IPC (PacketHeader + uint64_t args[8]) | NDJSON with typed fields | This phase | Eliminates FloatToU64 bugs, self-documenting, debuggable with jq |
| No dirty checking (send every frame) | Per-entity dirty cache | This phase | ~90% traffic reduction for stationary actors |

**Eliminated by this phase:**
- `FloatToU64()` / `U64ToFloat()` -- source of struct alignment bugs between MinGW and GCC
- `HookEventPacket` / `CommandPacket` binary structs -- the alignment issue that motivated this phase
- Generic `args[8]` positional arguments -- replaced by named fields

## Open Questions

1. **HOOK_PROJECTILE special case**
   - What we know: Takes a raw `dataPtr` with no formId. The projectile launch data is a complex struct read by the native client via pread.
   - What's unclear: Should we serialize the projectile data inline in JSON (which fields?), or keep sending the raw pointer for pread access?
   - Recommendation: Send the pointer as `"data_ptr"` in JSON for now. The native client still uses pread to read projectile launch structs. Full struct serialization can be deferred.

2. **mock_skyrim_loader and fake_player updates**
   - What we know: `mock_skyrim_loader.cpp` connects to GameServer as a test client. `fake_player.cpp` is a headless mock. Neither uses the relay TCP protocol directly.
   - What's unclear: Whether integration tests in `test_ingame_sync.sh` depend on binary TCP format.
   - Recommendation: Check and update integration test scripts if they parse binary relay output. Mock skyrim loader and fake_player connect to GameServer (enet6), not the relay TCP, so they should be unaffected.

3. **Record file format (--record flag)**
   - What we know: `main.cpp` writes raw `HookEventPacket` structs to a binary record file.
   - What's unclear: Should recording switch to NDJSON format too?
   - Recommendation: Yes, switch to NDJSON recording. This makes recorded files human-readable and debuggable with `jq`.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | Code/tests/xmake.lua |
| Quick run command | `xmake run TPTests "[JsonProtocol]"` |
| Full suite command | `xmake run TPTests` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| JSON-01 | NDJSON round-trip: serialize hook event to JSON, parse back, verify fields | unit | `xmake run TPTests "[JsonProtocol]"` | No -- Wave 0 |
| JSON-02 | NDJSON round-trip: serialize command to JSON, parse back, verify fields | unit | `xmake run TPTests "[JsonProtocol]"` | No -- Wave 0 |
| JSON-03 | LineBuffer handles partial reads correctly | unit | `xmake run TPTests "[LineBuffer]"` | No -- Wave 0 |
| JSON-04 | Dirty cache returns true for changed position, false for unchanged | unit | `xmake run TPTests "[DirtyCache]"` | No -- Wave 0 |
| JSON-05 | Dirty cache clears on actor removal | unit | `xmake run TPTests "[DirtyCache]"` | No -- Wave 0 |
| JSON-06 | All 30 hook ops produce valid JSON with correct field names | unit | `xmake run TPTests "[JsonProtocol]"` | No -- Wave 0 |
| JSON-07 | All 15 command ops produce valid JSON with correct field names | unit | `xmake run TPTests "[JsonProtocol]"` | No -- Wave 0 |
| JSON-08 | End-to-end: DLL sends JSON, native receives and dispatches correctly | integration/manual | Manual (requires Skyrim) | No |

### Sampling Rate
- **Per task commit:** `xmake run TPTests "[JsonProtocol],[LineBuffer],[DirtyCache]"`
- **Per wave merge:** `xmake run TPTests`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `Code/tests/json_protocol_tests.cpp` -- covers JSON-01 through JSON-07
- [ ] `Code/tests/line_buffer_tests.cpp` -- covers JSON-03
- [ ] `Code/tests/dirty_cache_tests.cpp` -- covers JSON-04, JSON-05
- [ ] nlohmann/json.hpp downloaded and placed in include path accessible to test target

## Project Constraints (from CLAUDE.md)

- **Build system:** DLL cross-compiles via MinGW from Linux. nlohmann/json must compile cleanly under MinGW GCC with C++20.
- **No SKSE SDK dependency in relay DLL:** nlohmann/json is header-only, no new deps needed.
- **No spdlog in DLL (D-13):** Use `RelayLog()` / `OutputDebugStringA()` for DLL logging, not spdlog.
- **Naming conventions:** CamelCase classes, `a` prefix for args, `m_` for members, `s_` for statics, `g_` for globals.
- **No exceptions in DLL:** Use `json::parse(str, nullptr, false)` nothrow mode.
- **protocol.h is single source of truth:** Keep it as documentation reference. New `json_protocol.h` replaces it as the active wire format definition.

## Files Requiring Modification

### DLL Side (MinGW cross-compiled)
| File | Change | LOC Estimate |
|------|--------|-------------|
| `Code/relay_dll/nlohmann/json.hpp` | NEW: drop in single header | 0 (vendored) |
| `Code/relay_dll/json_protocol.h` | NEW: op names, field name constants | ~120 |
| `Code/relay_dll/dirty_cache.h` | NEW: per-entity dirty-check cache | ~80 |
| `Code/relay_dll/hook_trampolines.cpp` | MODIFY: all 31 trampolines build JSON instead of HookEventPacket | ~500 net change |
| `Code/relay_dll/tcp_server.h` | MODIFY: add SendJson(), ReceiveJsonLine(), LineBuffer | ~30 |
| `Code/relay_dll/tcp_server.cpp` | MODIFY: implement JSON send/receive, line buffering | ~60 |
| `Code/relay_dll/relay_main.cpp` | MODIFY: DrainCommandQueue parses JSON commands | ~60 |

### Native Client Side (Linux ELF)
| File | Change | LOC Estimate |
|------|--------|-------------|
| `Code/native_client/third_party/nlohmann/json.hpp` | NEW: drop in single header | 0 (vendored) |
| `Code/native_client/game_bridge/tcp_client.h` | MODIFY: add SendJsonLine(), ReceiveJsonLine(), LineBuffer | ~30 |
| `Code/native_client/game_bridge/tcp_client.cpp` | MODIFY: implement JSON send/receive, line buffering | ~60 |
| `Code/native_client/main.cpp` | MODIFY: dispatch on json["op"] instead of binary opcode | ~80 |
| `Code/native_client/Services/CharacterService.cpp` | MODIFY: OnHookEvent takes json, command sends build json | ~120 |
| `Code/native_client/Services/CharacterService.h` | MODIFY: signature change | ~5 |
| `Code/native_client/Services/WeatherService.cpp` | MODIFY: OnHookEvent + command sends | ~30 |
| `Code/native_client/Services/CalendarService.cpp` | MODIFY: OnHookEvent | ~20 |
| `Code/native_client/Services/QuestService.cpp` | MODIFY: OnHookEvent | ~20 |
| `Code/native_client/Services/CombatService.cpp` | MODIFY: OnHookEvent | ~30 |
| `Code/native_client/Services/InventoryService.cpp` | MODIFY: OnHookEvent + command sends | ~40 |
| `Code/native_client/Services/ActorValueService.cpp` | MODIFY: OnHookEvent + command sends | ~30 |
| `Code/native_client/Services/MagicService.cpp` | MODIFY: OnHookEvent + command sends | ~40 |
| All 8 service `.h` files | MODIFY: signature changes | ~8 total |

### Shared
| File | Change | LOC Estimate |
|------|--------|-------------|
| `Code/relay_dll/protocol.h` | KEEP: reference only, add deprecation comment | ~2 |
| `json_protocol.h` | Shared between DLL and native (or duplicated) | (counted above) |

### Tests
| File | Change | LOC Estimate |
|------|--------|-------------|
| `Code/tests/json_protocol_tests.cpp` | NEW: JSON round-trip tests for all ops | ~200 |
| `Code/tests/line_buffer_tests.cpp` | NEW: TCP line buffer edge cases | ~80 |
| `Code/tests/dirty_cache_tests.cpp` | NEW: dirty-check cache tests | ~80 |
| `Code/tests/relay_protocol_tests.cpp` | MODIFY: update or mark as legacy | ~10 |

**Total estimated changes:** ~1,500 LOC across ~25 files

## Sources

### Primary (HIGH confidence)
- Project source code: `protocol.h`, `hook_trampolines.cpp`, `tcp_server.cpp`, `tcp_client.cpp`, `main.cpp`, `CharacterService.cpp` -- direct inspection of current implementation
- nlohmann/json GitHub releases -- v3.11.3 confirmed as latest stable

### Secondary (MEDIUM confidence)
- [nlohmann/json official docs](https://json.nlohmann.me/) -- API patterns, nothrow parsing
- [nlohmann/json GitHub](https://github.com/nlohmann/json) -- MinGW/GCC compatibility confirmed via CI matrix

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - nlohmann/json is locked by D-01, well-known library
- Architecture: HIGH - protocol structure fully visible in source, all integration points identified
- Pitfalls: HIGH - TCP stream buffering and exception safety are well-documented C++ concerns
- Dirty cache design: MEDIUM - thresholds are estimates; may need tuning during gameplay testing

**Research date:** 2026-03-29
**Valid until:** 2026-04-28 (stable domain, no external API changes expected)
