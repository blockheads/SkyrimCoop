# Feature Landscape

**Domain:** Skyrim cooperative multiplayer mod (2-4 players, P2P host-based)
**Researched:** 2026-03-27
**Confidence:** MEDIUM-HIGH (based on official Tilted Online docs, community reports, codebase analysis)

## Table Stakes

Features users expect from a Skyrim co-op mod. Missing any of these and players will not use SkyrimCoop over Skyrim Together Reborn.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Character position/movement sync | Core of multiplayer -- seeing each other move | Low (existing) | Already implemented. Interpolation system exists. |
| Animation sync | Players look like they're playing the same game | Medium (existing, fragile) | Exists but BehaviorVar.cpp has unhandled hash collisions. Vampire lord/werewolf edge cases. |
| NPC sync (position, combat, death) | Enemies must be shared or combat is meaningless | Medium (existing) | Exists. Host-authoritative in co-op simplifies this vs STR's distributed model. |
| Dragon sync | Dragons are Skyrim's signature encounter | Medium (existing) | Already synced in STR. Must preserve. |
| Inventory/equipment sync | See each other's gear, share loot | Medium (existing) | Exists. Equipment appearance sync critical for immersion. |
| Combat sync (melee, ranged, magic) | Fighting together is the point | High (existing, imperfect) | Exists. Enemies sometimes become invincible after 0 HP -- known STR issue to fix. |
| Quest sync (host-authoritative) | Playing the story together | High (existing, fragile) | STR's biggest pain point. Only leader can interact with quest NPCs. Desync common. |
| Spell/magic effect sync | Mages need to work | Medium (existing) | Exists. Player-targeting fear-type spells disabled (correct approach). |
| Stealth sync | Stealth builds need to function | Low (existing) | Exists. AI detection for other players disabled (correct approach). |
| Lock sync | Dungeons need to work together | Low (existing) | Exists. Puzzle doors (dragon claw) also synced. |
| Weather sync | Visual consistency between players | Low (existing) | Host determines weather. Already implemented. |
| Time sync | Day/night must match | Low (existing) | Server determines world time. Wait/sleep disabled. |
| Party system | Group management, who's connected | Low (existing) | Exists. Party leader concept already in place. |
| Overlay UI (connect, party, chat) | Players need to find and talk to each other | Medium (existing) | Angular/CEF overlay exists. F2/RCtrl to open. |
| Beastform sync (werewolf/vampire lord) | Transformation builds are popular | Medium (existing) | Exists but animation sync has known TODOs in code. |
| Actor value sync (health/stamina/magicka) | Need to see ally health in combat | Low (existing) | Exists. ActorValueService has crash bug (CONCERNS.md). |
| Horse mounting sync | Traversal feature | Low (existing) | Exists. |
| Chest/container sync | Looting together | Medium (existing) | Exists. Players can loot simultaneously. |
| XP sharing | Co-op players expect shared progression | Low (existing) | Combat skill XP synced across party. |

## Differentiators

Features that would set SkyrimCoop apart from Skyrim Together Reborn. These address STR's most complained-about gaps.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **Zero-setup hosting (Steam invite)** | "Host clicks host, friend joins via Steam invite, it just works." No server browser, no IP config, no port forwarding. | High | Core project differentiator. Replaces STR's dedicated server model. Requires Steam/EOS lobby integration. |
| **Linux-native development** | Linux-first toolchain attracts developer contributors. Steam Deck users benefit from better Proton compatibility testing. | High | Core project differentiator. LethalInjection pattern. |
| **Improved quest sync reliability** | STR's number one complaint: quests desync, break, corrupt. Host-authoritative P2P should make this more reliable since there's no distributed ownership confusion. | High | Architectural advantage of P2P model. Must validate quest state on peer join, detect and recover from desync. |
| **Direct item trading UI** | STR has no trade system. Players resort to dropping items (buggy, items invisible to others). | Medium | New feature. Simple offer/accept dialog. Huge QoL improvement over STR. Community's 4th most requested feature. |
| **Player visibility system** | STR players constantly lose each other. Name tags, distance indicators, or compass markers for party members. | Low-Medium | New UI feature. Compass markers are most immersive option. Low complexity, high impact. |
| **Ping/waypoint system** | Point at things to communicate without voice. Mark items, locations, enemies for party. | Medium | New feature. Needs 3D world-space markers synced between players. |
| **Host-authoritative wait/sleep** | STR disabled wait/sleep entirely. Allow host to trigger wait, affecting all players simultaneously. | Medium | Addresses STR complaint. Need to handle what happens to non-host players during time skip (teleport? freeze? fade to black?). |
| **Mod-list enforcement on connect** | Host's mod list is authoritative; clients must match. Clear error messages on mismatch. | Medium | Already in project requirements. STR has this partially but error messages are poor. |
| **Reconnection with state recovery** | STR has no reconnect -- disconnect means restart. P2P model should allow rejoining host session with state resync. | High | Requires state snapshot/delta mechanism. Huge UX improvement for unstable connections. |
| **Non-leader NPC interaction** | STR forces only leader to talk to NPCs. Allow non-leaders to interact with non-quest NPCs (merchants, trainers) without causing desync. | Medium | Requires classifying NPCs as quest-critical vs. general. General NPCs safe for anyone to interact with. |
| **Desync detection and recovery** | STR has silent desync with no recovery. Add periodic state hash comparison, warning UI, and manual resync command. | High | Addresses networking resilience gaps noted in CONCERNS.md. State hash checksums + rollback mechanism. |
| **AMD GPU support** | STR/current codebase explicitly blocks AMD GPUs. Fixing this opens the mod to a large hardware segment. | Low-Medium | CONCERNS.md notes this is explicitly disabled. Investigate and re-enable. |

## Anti-Features

Features to explicitly NOT build. These are tempting but would add complexity that contradicts the co-op focus or create maintenance burden.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Dedicated server support** | Contradicts P2P co-op model. Adds infrastructure burden, splits codebase into two deployment modes. STR already does this. | Host-embedded server only. If someone wants dedicated, they can use STR. |
| **Server browser / server list** | MMO feature, not co-op. Adds discovery infrastructure, moderation needs, spam. | Steam/EOS friend invite only. LAN discovery as stretch goal. |
| **More than 4 players** | Skyrim's combat, AI, and quest systems break down at scale. Co-op, not MMO. | Hard cap at 4. Optimize for 2-player experience, test up to 4. |
| **PvP combat system** | STR explicitly says PvP is broken and not their focus. Adds attack validation, anti-cheat, balance concerns. | Disable PvP entirely. Two friends don't need to stab each other. |
| **Voice chat integration** | Discord and Steam voice exist. Building VOIP adds latency, codec, echo cancellation complexity. | Document "use Discord/Steam voice" in setup guide. |
| **Automatic mod downloading** | Legal liability (redistributing mod files), bandwidth costs, mod author permission issues. Complex to implement correctly. | Clear mod-list mismatch errors with links to download pages. |
| **Follower sync** | STR explicitly discourages followers -- AI breaks in multiplayer. You have real human companions now. | Document as unsupported. Followers stay local/invisible to peers. |
| **DLC home sync** | STR intentionally doesn't sync player homes. Low value, high complexity (furniture placement, crafting stations, mannequins). | Each player has their own home state. Not synced. |
| **Real-time lip sync for NPCs** | Extremely complex for minimal visual payoff in co-op where you're watching from third person. | Sync dialogue text/subtitles and quest state. Lip sync is local-only. |
| **Admin web panel** | Designed for dedicated servers. In P2P, the host IS the admin. | Simple in-game host controls overlay. Repurpose existing CEF UI. |
| **Lua scripting API for end users** | Maintenance burden. Server-side scripting designed for dedicated servers. Co-op host doesn't need custom scripts. | Remove or freeze Lua API. Use it internally only if needed for game logic. |

## Feature Dependencies

```
Steam/EOS Lobby Integration → Zero-Setup Hosting → Drop-in Join
P2P Refactor (World merge) → Host-Authoritative Quest Sync → Improved Quest Reliability
P2P Refactor (NetworkBridge) → Reconnection with State Recovery
P2P Refactor (NetworkBridge) → Desync Detection (state hashing)
Character Sync (existing) → Player Visibility System (name tags/compass markers)
Inventory Sync (existing) → Direct Item Trading UI
Overlay UI (existing) → Ping/Waypoint System
Overlay UI (existing) → Trading UI
Overlay UI (existing) → Host Wait/Sleep Controls
NPC Classification (quest vs general) → Non-Leader NPC Interaction
ActorValueService crash fix → Reliable Actor Value Sync → Combat feels right
AnimationSystem fixes (BehaviorVar) → Reliable Animation Sync → No sliding/T-posing
Mod-list hash comparison → Mod-list Enforcement on Connect
```

## MVP Recommendation

### Phase 1: Make What Exists Work Reliably on P2P

The existing STR feature set is comprehensive. The problem is reliability, not features. Prioritize:

1. **P2P architecture refactor** -- the foundation everything else depends on
2. **Fix ActorValueService crash** -- silent crashes destroy trust
3. **Fix BehaviorVar animation hash collisions** -- sliding NPCs are the most visible bug
4. **Combat death resolution** -- enemies stuck at 0 HP is game-breaking
5. **AMD GPU support** -- re-enable, test, and fix or document why not

### Phase 2: Core Differentiators

The features that justify using SkyrimCoop over STR:

1. **Zero-setup hosting via Steam invite** -- the headline feature
2. **Mod-list enforcement with clear errors** -- prevents most support issues
3. **Player visibility (compass markers)** -- trivial effort, massive QoL
4. **Direct item trading UI** -- simple UI, eliminates drop-item workaround

### Phase 3: Reliability and Polish

1. **Desync detection and recovery** -- state hash comparison, warning UI
2. **Reconnection with state resync** -- don't lose progress on disconnect
3. **Host-authoritative wait/sleep** -- quality of life
4. **Non-leader NPC interaction** (merchants/trainers only) -- co-op quality of life

### Defer Indefinitely

- **Ping/waypoint system** -- nice but not essential for 2-player co-op where you use voice
- **Automatic mod downloading** -- legal and technical minefield
- **PvP** -- not the product
- **Follower sync** -- you have human friends now

## E2E Testing for Sync Features

Research into automated testing for multiplayer game mods reveals no established framework for this specific domain. The recommended approach (informed by Metaplay's BotClient pattern and general multiplayer testing practice):

| Approach | What It Tests | Complexity | Notes |
|----------|--------------|------------|-------|
| **Headless mock client** | Message serialization, state sync, reconnection | High (initial), Low (ongoing) | Build a lightweight C++ client that connects to embedded server, sends/receives messages without Skyrim. Tests networking layer in CI. |
| **State hash assertions** | Desync detection | Medium | Host and mock client both compute world state hash. Assert they match after N operations. |
| **Scripted scenario replay** | Quest progression, combat sequences | High | Record real gameplay message sequences, replay through mock client, assert expected state. |
| **Fuzz testing on messages** | Crash resistance, validation | Medium | Send malformed/random messages to server, assert no crashes. Addresses CONCERNS.md "Game State Validation Absent." |

The headless mock client is the most valuable investment. It enables CI testing of all networking code without requiring Skyrim or Windows. Build it to simulate: connect, spawn character, move, interact, disconnect, reconnect.

## Sources

- [Tilted Online Official Features List](https://wiki.tiltedphoques.com/tilted-online/general-information/features) -- HIGH confidence, official documentation
- [Tilted Online Playguide](https://wiki.tiltedphoques.com/tilted-online/general-information/playguide) -- HIGH confidence, official rules/limitations
- [Tilted Online FAQ](https://wiki.tiltedphoques.com/tilted-online/general-information/faq) -- HIGH confidence
- [10 Changes To Make STR Great (TheGamer)](https://www.thegamer.com/skyrim-together-reborn-multiplayer-mod-good-to-great/) -- MEDIUM confidence, community perspective
- [STR Nexus Mods Page](https://www.nexusmods.com/skyrimspecialedition/mods/69993) -- HIGH confidence, primary distribution
- [TiltedEvolution GitHub Issues](https://github.com/tiltedphoques/TiltedEvolution/issues) -- HIGH confidence, actual bug reports
- [Metaplay BotClient Testing](https://docs.metaplay.io/feature-cookbooks/automated-testing/botclient-testing.html) -- MEDIUM confidence, testing pattern reference
- [STR Mod Compatibility GitHub](https://github.com/tiltedphoques/Mod-Compatibility) -- HIGH confidence, official tracker
- [Skyrim Together Tweaks (Nexus)](https://www.nexusmods.com/skyrimspecialedition/mods/135782) -- MEDIUM confidence, community fix mod
- Project codebase analysis (CLAUDE.md, CONCERNS.md, PROJECT.md) -- HIGH confidence, direct inspection
