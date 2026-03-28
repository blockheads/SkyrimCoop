#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <atomic>
#include <chrono>

#include <spdlog/spdlog.h>
#include <rpmalloc.h>

#include "game_bridge/proc_memory.h"
#include "game_bridge/pointer_table.h"
#include "game_bridge/tcp_client.h"
#include "game_bridge/game_reader.h"
#include "protocol.h"

#include "Services/WeatherService.h"
#include "Services/CalendarService.h"
#include "Services/QuestService.h"
#include "Services/CombatService.h"

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

static void HandleHookEvent(const PacketHeader& aHeader, const uint8_t* apPayload,
                            GameReader& aReader, PointerTable& aPtrs) {
    // Hook event packets have the HookEventPacket layout (minus header)
    if (aHeader.length < sizeof(HookEventPacket) - sizeof(PacketHeader)) {
        spdlog::warn("Hook event packet too small: {} bytes", aHeader.length);
        return;
    }

    // Payload starts after header: argCount(1) + pad(7) + args(8*8)
    const uint8_t argCount = apPayload[0];
    const uint64_t* pArgs = reinterpret_cast<const uint64_t*>(apPayload + 8);

    switch (static_cast<HookOpcode>(aHeader.opcode)) {
    case HOOK_ACTOR_ADDED: {
        if (argCount < 1) break;
        uint64_t actorPtr = pArgs[0];
        // Read formId from the actor pointer via /proc/pid/mem
        uint32_t formId = aReader.ReadFormId(actorPtr);
        if (formId != 0) {
            aPtrs.Insert(formId, actorPtr);
            spdlog::info("Actor added: formId={:#x} ptr={:#x}", formId, actorPtr);
        } else {
            spdlog::warn("Actor added but could not read formId at ptr={:#x}", actorPtr);
        }
        break;
    }
    case HOOK_ACTOR_REMOVED: {
        if (argCount < 1) break;
        uint64_t actorPtr = pArgs[0];
        // Read formId to remove from table, fallback to remove by pointer
        uint32_t formId = aReader.ReadFormId(actorPtr);
        if (formId != 0) {
            aPtrs.Remove(formId);
            spdlog::info("Actor removed: formId={:#x}", formId);
        } else {
            aPtrs.RemoveByPointer(actorPtr);
            spdlog::info("Actor removed by ptr={:#x}", actorPtr);
        }
        break;
    }
    default:
        spdlog::debug("Unhandled hook event: opcode={:#x} args={}", aHeader.opcode, argCount);
        break;
    }
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

    // Initialize services (all take GameReader& for memory reads)
    WeatherService weatherService(gameReader, tcpClient);
    CalendarService calendarService(gameReader, tcpClient);
    QuestService questService(gameReader, tcpClient);
    CombatService combatService(gameReader, tcpClient);

    spdlog::info("All services initialized");

    // Main event loop
    spdlog::info("Entering main event loop");
    PacketHeader header{};
    uint8_t payload[sizeof(HookEventPacket)]; // max payload size

    // Simple delta time tracking for service Update() calls
    auto lastTime = std::chrono::steady_clock::now();

    while (s_running.load(std::memory_order_relaxed)) {
        if (!tcpClient.Receive(&header, payload, sizeof(payload))) {
            spdlog::warn("TCP receive failed or connection closed");
            break;
        }

        // Calculate delta time for service updates
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        if (header.opcode >= 0xF000) {
            // Control packet
            HandleControlPacket(header, tcpClient);
        } else if (header.opcode < 0x8000) {
            // Hook event from DLL -- dispatch to core handler and services
            HandleHookEvent(header, payload, gameReader, pointerTable);

            // Reconstruct HookEventPacket for service dispatch
            HookEventPacket hookPkt{};
            hookPkt.header = header;
            if (header.length >= 1) {
                hookPkt.argCount = payload[0];
                if (hookPkt.argCount > 8) hookPkt.argCount = 8;
                memcpy(hookPkt.args, payload + 8, hookPkt.argCount * sizeof(uint64_t));
            }

            // Dispatch to services
            weatherService.OnHookEvent(hookPkt);
            calendarService.OnHookEvent(hookPkt);
            questService.OnHookEvent(hookPkt);
            combatService.OnHookEvent(hookPkt);
        } else {
            // Command acknowledgments (0x8000-0xEFFF) -- not expected from DLL
            spdlog::warn("Unexpected command-range opcode from DLL: {:#x}", header.opcode);
        }

        // Periodic service updates
        weatherService.Update(deltaTime);
        calendarService.Update(deltaTime);
        questService.Update(deltaTime);
        combatService.Update(deltaTime);
    }

    // Cleanup
    spdlog::info("Shutting down native client");
    tcpClient.Disconnect();
    memReader.Close();
    rpmalloc_finalize();

    return 0;
}
