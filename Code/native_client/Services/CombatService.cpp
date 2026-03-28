#include "CombatService.h"

#include <spdlog/spdlog.h>
#include <cstring>

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"
#include "protocol.h"

// Game struct offsets
static constexpr uint64_t cFormIdOffset = 0x14;

// Utility: extract float from uint64_t (reverse of FloatToU64 in DLL)
static float U64ToFloat(uint64_t aVal)
{
    float f = 0.0f;
    memcpy(&f, &aVal, sizeof(float));
    return f;
}

CombatService::CombatService(GameReader& aReader, TcpClient& aClient)
    : m_reader(aReader)
    , m_client(aClient)
{
    spdlog::info("CombatService initialized");
}

void CombatService::OnHookEvent(const HookEventPacket& aPkt)
{
    switch (static_cast<HookOpcode>(aPkt.header.opcode)) {
    case HOOK_DAMAGE_ACTOR: {
        // args[0] = Actor* (target), args[1] = float (damage amount, bit-cast)
        if (aPkt.argCount < 2) break;

        uint64_t actorPtr = aPkt.args[0];
        float damage = U64ToFloat(aPkt.args[1]);

        // Read target formId via memory
        uint32_t targetFormId = m_reader.ReadFormId(actorPtr);

        if (targetFormId != 0) {
            // TODO(Plan 07+): Send damage sync via network transport
            spdlog::info("CombatService: DamageActor target={:#x} damage={:.2f}",
                         targetFormId, damage);
            m_damageEventCount++;
        }
        break;
    }
    case HOOK_PROJECTILE: {
        // args[0] = LaunchData* (pointer to projectile launch data)
        if (aPkt.argCount < 1) break;

        uint64_t launchDataPtr = aPkt.args[0];

        // The LaunchData struct contains shooter, projectile base, weapon, ammo, origin, angles
        // For now: log the raw pointer. Full field extraction requires known offsets
        // which will be added when ProjectileLaunchRequest is wired.
        spdlog::info("CombatService: Projectile launched, launchData={:#x}", launchDataPtr);
        m_projectileEventCount++;
        break;
    }
    default:
        break;
    }
}

void CombatService::Update(float /*aDeltaTime*/)
{
    // Combat is event-driven via hooks, not polled.
    // Periodic logging for debugging:
    static uint32_t s_lastLoggedDamage = 0;
    if (m_damageEventCount > s_lastLoggedDamage + 10) {
        spdlog::debug("CombatService: {} damage events, {} projectile events total",
                      m_damageEventCount, m_projectileEventCount);
        s_lastLoggedDamage = m_damageEventCount;
    }
}
