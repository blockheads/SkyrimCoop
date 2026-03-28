#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>
#include <rpmalloc.h>
#include <entt/entt.hpp>

#include "game_bridge/proc_memory.h"
#include "game_bridge/pointer_table.h"
#include "game_bridge/tcp_client.h"
#include "game_bridge/game_reader.h"
#include "protocol.h"

// Services (all 8)
#include "Services/WeatherService.h"
#include "Services/CalendarService.h"
#include "Services/QuestService.h"
#include "Services/CombatService.h"
#include "Services/InventoryService.h"
#include "Services/ActorValueService.h"
#include "Services/MagicService.h"
#include "Services/CharacterService.h"

// Systems
#include "Systems/InterpolationSystem.h"
#include "Systems/AnimationSystem.h"

static std::atomic<bool> s_running{true};

static void SignalHandler(int /*sig*/) {
    s_running.store(false, std::memory_order_relaxed);
}

static void PrintUsage(const char* apProgram) {
    fprintf(stderr, "Usage: %s --pid <pid> --port <port>\n", apProgram);
    fprintf(stderr, "  --pid   Target Skyrim process ID\n");
    fprintf(stderr, "  --port  DLL TCP server port\n");
    fprintf(stderr, "  --help  Show this help\n");
}

static bool ParseArgs(int argc, char* argv[], pid_t& aOutPid, uint16_t& aOutPort) {
    aOutPid = 0;
    aOutPort = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            aOutPid = static_cast<pid_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            aOutPort = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            exit(0);
        }
    }

    return aOutPid > 0 && aOutPort > 0;
}

// Dispatch a hook event to the pointer table and all services
static void DispatchHookEvent(const HookEventPacket& aHookPkt,
                               GameReader& aReader, PointerTable& aPtrs,
                               WeatherService& aWeather, CalendarService& aCalendar,
                               QuestService& aQuest, CombatService& aCombat,
                               InventoryService& aInventory, ActorValueService& aActorValue,
                               MagicService& aMagic, CharacterService& aCharacter)
{
    // Core pointer table tracking for actor add/remove
    switch (static_cast<HookOpcode>(aHookPkt.header.opcode)) {
    case HOOK_ACTOR_ADDED: {
        if (aHookPkt.argCount >= 1 && aHookPkt.args[0] != 0) {
            uint64_t actorPtr = aHookPkt.args[0];
            uint32_t formId = aReader.ReadFormId(actorPtr);
            if (formId != 0) {
                aPtrs.Insert(formId, actorPtr);
            }
        }
        break;
    }
    case HOOK_ACTOR_REMOVED: {
        if (aHookPkt.argCount >= 1 && aHookPkt.args[0] != 0) {
            uint64_t actorPtr = aHookPkt.args[0];
            uint32_t formId = aReader.ReadFormId(actorPtr);
            if (formId != 0) {
                aPtrs.Remove(formId);
            } else {
                aPtrs.RemoveByPointer(actorPtr);
            }
        }
        break;
    }
    default:
        break;
    }

    // Dispatch to all services
    aWeather.OnHookEvent(aHookPkt);
    aCalendar.OnHookEvent(aHookPkt);
    aQuest.OnHookEvent(aHookPkt);
    aCombat.OnHookEvent(aHookPkt);
    aInventory.OnHookEvent(aHookPkt);
    aActorValue.OnHookEvent(aHookPkt);
    aMagic.OnHookEvent(aHookPkt);
    aCharacter.OnHookEvent(aHookPkt);
}

static void HandleControlPacket(const PacketHeader& aHeader, TcpClient& aClient) {
    switch (static_cast<ControlOpcode>(aHeader.opcode)) {
    case CTRL_HEARTBEAT:
        // Respond with heartbeat
        aClient.SendControl(CTRL_HEARTBEAT);
        break;
    case CTRL_SHUTDOWN:
        spdlog::info("Received CTRL_SHUTDOWN from DLL");
        s_running.store(false, std::memory_order_relaxed);
        break;
    case CTRL_READY:
        spdlog::info("DLL confirmed CTRL_READY");
        break;
    default:
        spdlog::warn("Unknown control opcode: {:#x}", aHeader.opcode);
        break;
    }
}

// Note: non-blocking receive via poll() can be added when TcpClient exposes
// the raw socket fd. For now, blocking receive with frame pacing is used.

int main(int argc, char* argv[]) {
    // Initialize rpmalloc
    rpmalloc_initialize();

    // Initialize spdlog
    spdlog::set_level(spdlog::level::info);

    // Parse command line
    pid_t targetPid = 0;
    uint16_t port = 0;
    if (!ParseArgs(argc, argv, targetPid, port)) {
        PrintUsage(argv[0]);
        rpmalloc_finalize();
        return 1;
    }

    spdlog::info("skyrim-coop native client starting (pid={}, port={})", targetPid, port);

    // Install signal handlers for clean shutdown
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Open /proc/pid/mem reader
    ProcMemReader memReader;
    if (!memReader.Open(targetPid)) {
        spdlog::error("Failed to open /proc/{}/mem -- is the process running?", targetPid);
        rpmalloc_finalize();
        return 1;
    }

    // Connect TCP client to DLL server
    TcpClient tcpClient;
    if (!tcpClient.Connect(port)) {
        spdlog::error("Failed to connect to DLL TCP server at 127.0.0.1:{}", port);
        memReader.Close();
        rpmalloc_finalize();
        return 1;
    }

    // Send CTRL_READY to signal native process is initialized
    if (!tcpClient.SendControl(CTRL_READY)) {
        spdlog::error("Failed to send CTRL_READY");
        tcpClient.Disconnect();
        memReader.Close();
        rpmalloc_finalize();
        return 1;
    }
    spdlog::info("Sent CTRL_READY to DLL");

    // Initialize game bridge
    PointerTable pointerTable;
    GameReader gameReader(memReader, pointerTable);

    // Initialize EnTT registry for ECS
    entt::registry registry;

    // Initialize all 8 services
    WeatherService weatherService(gameReader, tcpClient);
    CalendarService calendarService(gameReader, tcpClient);
    QuestService questService(gameReader, tcpClient);
    CombatService combatService(gameReader, tcpClient);
    InventoryService inventoryService(gameReader, tcpClient);
    ActorValueService actorValueService(gameReader, tcpClient);
    MagicService magicService(gameReader, tcpClient);
    CharacterService characterService(gameReader, tcpClient, registry);

    spdlog::info("All 8 services initialized (Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic, Character)");

    // Main event loop
    spdlog::info("Entering main event loop");
    PacketHeader header{};
    uint8_t payload[sizeof(HookEventPacket)]; // max payload size

    // Delta time tracking
    auto lastTime = std::chrono::steady_clock::now();

    // Heartbeat tracking (send every 5 seconds)
    float heartbeatTimer = 0.0f;
    static constexpr float kHeartbeatInterval = 5.0f;

    // Target frame time (~60 FPS = 16ms per iteration)
    static constexpr auto kTargetFrameTime = std::chrono::milliseconds(16);

    // Get socket fd for poll() -- TcpClient exposes this indirectly via IsConnected
    // We need the raw fd. Since TcpClient doesn't expose it, we'll use a different approach:
    // Process all available packets in a batch, then sleep for remainder of frame.

    while (s_running.load(std::memory_order_relaxed)) {
        auto frameStart = std::chrono::steady_clock::now();

        // Calculate delta time
        float deltaTime = std::chrono::duration<float>(frameStart - lastTime).count();
        lastTime = frameStart;

        // Clamp delta time to prevent spiral of death
        if (deltaTime > 0.25f)
            deltaTime = 0.25f;

        // --- Phase 1: Receive and dispatch all pending TCP packets ---
        // Process up to 64 packets per frame to prevent starvation
        int packetsProcessed = 0;
        static constexpr int kMaxPacketsPerFrame = 64;

        while (packetsProcessed < kMaxPacketsPerFrame) {
            if (!tcpClient.IsConnected()) {
                spdlog::warn("TCP connection lost");
                s_running.store(false, std::memory_order_relaxed);
                break;
            }

            // Try non-blocking receive using the client's Receive with a poll check
            // Since TcpClient::Receive is blocking, we use it only when we know data is ready
            // For now: use blocking receive with a packet batch approach
            // The first receive call will block briefly (TCP socket), subsequent ones
            // return quickly if no data.
            if (!tcpClient.Receive(&header, payload, sizeof(payload))) {
                spdlog::warn("TCP receive failed or connection closed");
                s_running.store(false, std::memory_order_relaxed);
                break;
            }

            packetsProcessed++;

            if (header.opcode >= 0xF000) {
                // Control packet
                HandleControlPacket(header, tcpClient);
            } else if (header.opcode < 0x8000) {
                // Hook event from DLL -- reconstruct HookEventPacket
                HookEventPacket hookPkt{};
                hookPkt.header = header;
                if (header.length >= 1) {
                    hookPkt.argCount = payload[0];
                    if (hookPkt.argCount > 8) hookPkt.argCount = 8;
                    memcpy(hookPkt.args, payload + 8, hookPkt.argCount * sizeof(uint64_t));
                }

                // Dispatch to pointer table and all services
                DispatchHookEvent(hookPkt, gameReader, pointerTable,
                                  weatherService, calendarService, questService, combatService,
                                  inventoryService, actorValueService, magicService, characterService);
            } else {
                // Command acknowledgments (0x8000-0xEFFF) -- not expected from DLL
                spdlog::warn("Unexpected command-range opcode from DLL: {:#x}", header.opcode);
            }

            break; // One packet per blocking call, then update
        }

        // --- Phase 2: Service updates ---
        weatherService.Update(deltaTime);
        calendarService.Update(deltaTime);
        questService.Update(deltaTime);
        combatService.Update(deltaTime);
        inventoryService.Update(deltaTime);
        actorValueService.Update(deltaTime);
        magicService.Update(deltaTime);
        characterService.Update(deltaTime);

        // --- Phase 3: Systems ---
        InterpolationSystem::Update(registry, deltaTime);
        AnimationSystem::Update(registry, tcpClient, deltaTime);

        // --- Phase 4: Heartbeat ---
        heartbeatTimer += deltaTime;
        if (heartbeatTimer >= kHeartbeatInterval) {
            heartbeatTimer = 0.0f;
            if (!tcpClient.SendControl(CTRL_HEARTBEAT)) {
                spdlog::warn("Failed to send heartbeat");
            }
        }

        // --- Phase 5: Frame pacing ---
        auto frameEnd = std::chrono::steady_clock::now();
        auto frameElapsed = frameEnd - frameStart;
        if (frameElapsed < kTargetFrameTime) {
            std::this_thread::sleep_for(kTargetFrameTime - frameElapsed);
        }
    }

    // Cleanup
    spdlog::info("Shutting down native client");
    tcpClient.Disconnect();
    memReader.Close();
    rpmalloc_finalize();

    return 0;
}
