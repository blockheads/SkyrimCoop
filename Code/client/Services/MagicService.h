#pragma once

#include <Events/Listen.h>
#include <Events/AddTargetEvent.h>
#include <Messages/Relayed.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <NoCopyMove.h>
#include <Structs/CastPart.h>
#include <unordered_map>

class IActorBridge;
class IFormBridge;

struct World;
struct TransportService;

struct UpdateEvent;
struct SpellCastEvent;
struct InterruptCastEvent;
struct AddTargetEvent;
struct RemoveSpellEvent;


/**
 * @brief Handles magic spell casting and magic effects.
 *
 * To the poor sod that thinks of venturing into this part of the code base..
 * Yes, the magic sync code is a mess. It is easily the ugliest part of the codebase.
 * I did not get around to refactoring this before going open source, but it definitely should be.
 * I'm sorry, and good luck.
 *
 * Contact cosideci for more info.
 */
struct MagicService
{
    MagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                 IActorBridge& aActorBridge, IFormBridge& aFormBridge) noexcept;
    ~MagicService() noexcept = default;

    NO_COPY_MOVE(MagicService);

  protected:
    /**
     * @brief Checks to apply queued effects on each frame.
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    // This game's actor holds a cast (concentration, ward, invisibility): its cast part (kCast) carries it.
    void OnSpellCastEvent(const SpellCastEvent& acSpellCastEvent) noexcept;
    // A held cast's end clears its slot in the part; any other cast's interruption goes as InterruptCastRequest.
    void OnInterruptCastEvent(const InterruptCastEvent& acEvent) noexcept;
    /**
     * @brief Interrupts a spell cast based on a server message.
     */
    void OnNotifyInterruptCast(const NotifyInterruptCast& acMessage) const noexcept;
    /**
     * @brief Sends magic effect and its target to the server.
     */
    void OnAddTargetEvent(const AddTargetEvent& acEvent) noexcept;
    /**
     * @brief Applies a magic effect based on a server message.
     */
    void OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept;
    /**
     * @brief Sends a message to remove a spell from a player.
     */
    void OnRemoveSpellEvent(const RemoveSpellEvent& acEvent) noexcept;
    /*
     * @brief Handles removal of a spell
     */
    void OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept;

  private:
    /**
     * Sometimes, certain magic effects are applied on actors that do not yet exist
     * within the client's entity component system (for example when a beast is summoned).
     * This function periodically checks whether the queued magic effects' targets exist already
     * and if so, sends out a message to add the magic effect to the target.
     */
    void ApplyQueuedEffects() noexcept;
    // A copy's cast part: a slot newly held is cast on it, one cleared interrupted (piece 9).
    std::vector<uint32_t> ApplyCast(uint32_t aId, const std::string& acBytes) noexcept;
    void CastOn(uint32_t aFormId, uint32_t aSource, const HeldCast& acCast) const noexcept;
    void OnCopyRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;


    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IFormBridge& m_formBridge;

    std::vector<entt::scoped_connection> m_connections;
    std::unordered_map<uint32_t, CastPart> m_held; // this game's casters' held casts, by form id
    struct Applied
    {
        uint32_t FormId{};
        CastPart Part;
    };
    std::unordered_map<uint32_t, Applied> m_applied; // what each copy was given, by session id

    /*
     * @brief Queued magic effects.
     * @see ApplyQueuedEffects
     * Tracks effects queued for incompletely constructed actors / targets
     * There may be multiple. There may be sequencing between the targets
     * that matters. And, the objects might also be destroyed
     * before application, so they must time out
     */
  public:
    class MagicQueue
    {
      public:
        MagicQueue() = default;
        MagicQueue(const MagicQueue&) = default;
        ~MagicQueue() noexcept = default;

        bool Expired() const noexcept { return std::chrono::steady_clock::now() > m_expiration; }
        static spdlog::level::level_enum m_logLevel; // Initializer at file end, non-const so debugger can change.
        template <typename... Args> static inline void Spdlog(spdlog::format_string_t<Args...> aFmt, Args&&... args)
        {
            spdlog::log(m_logLevel, aFmt, std::forward<Args>(args)...);
        }

      private:
        const std::chrono::steady_clock::duration m_queueLimit{std::chrono::seconds(4)};
        const std::chrono::steady_clock::time_point m_expiration{std::chrono::steady_clock::now() + m_queueLimit};
    };

    class MagicAddTargetEventQueue : public MagicQueue
    {
      public:
        MagicAddTargetEventQueue() = default;
        MagicAddTargetEventQueue(const AddTargetEvent& aTarget) : m_AddTargetEvent(aTarget) {};
        ~MagicAddTargetEventQueue() noexcept = default;
        const AddTargetEvent& Target() const noexcept           { return m_AddTargetEvent; }

      private:
        AddTargetEvent m_AddTargetEvent;
    };

    class MagicNotifyAddTargetQueue : public MagicQueue
    {
      public:
        MagicNotifyAddTargetQueue() = default;
        MagicNotifyAddTargetQueue(const NotifyAddTarget& aTarget) : m_NotifyAddTarget(aTarget) {};
        ~MagicNotifyAddTargetQueue() noexcept = default;
        const NotifyAddTarget& Target() const noexcept            { return m_NotifyAddTarget; }

      private:
        NotifyAddTarget m_NotifyAddTarget;
    };

  private:
    std::queue<MagicAddTargetEventQueue> m_queuedEffects;
    std::queue<MagicNotifyAddTargetQueue> m_queuedRemoteEffects;
};

// Exposed so we can increase log level of just this tricky code, in debugger or a build.
// Non-const has to be initialized outside of class.
inline spdlog::level::level_enum MagicService::MagicQueue::m_logLevel{spdlog::level::debug};
