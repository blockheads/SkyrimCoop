#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <Services/JoinStatus.h>
#include <Services/SaveTransfer.h>
#include <Structs/PlayerRecord.h>

#include <optional>
#include <string>

class IJoinBridge;
struct AuthenticationResponse;
struct ConnectedEvent;
struct DisconnectedEvent;
struct GameLoadedEvent;
struct GameLoadFailedEvent;
struct NotifyPartyReload;
struct NotifySaveChunk;
struct UpdateEvent;
struct World;

/**
 * @brief Joining a session (docs/design/join-flow.md, "As built"): the host's gate, the host's save received and
 * loaded, this player's character put on the save's player (the host's save's record of it, else the one its own game
 * brought), then the usual join. Every game runs it; a front end (the join screen, a Steam invite, the harness)
 * starts it.
 */
struct JoinService
{
    // The names the two games' saves are written under.
    static constexpr const char* kJoinSave = "SkyrimCoopJoin";
    static constexpr const char* kTransferSave = "SkyrimCoopTransfer";
    static constexpr double kConnectSeconds = 20.0;
    static constexpr double kChunkSeconds = 30.0;
    static constexpr double kLoadSeconds = 180.0;
    // A guest's session that drops: the same host again, this many times, each try this long at most.
    static constexpr int kReconnectTries = 3;
    static constexpr double kReconnectSeconds = 10.0;
    static constexpr double kRetrySeconds = 2.0;

    JoinService(World& aWorld, entt::dispatcher& aDispatcher, IJoinBridge& aBridge) noexcept;

    NO_COPY_MOVE(JoinService);

    // Joins the session at acEndpoint, from a game this player plays (its character comes along) or from the main
    // menu; false when this game is busy or in a session (one it hosts alone it leaves first).
    bool Join(const std::string& acEndpoint) noexcept;
    // A terminal session exit; internal save loads never use this.
    void Cancel(const std::string& acReason) noexcept;
    // A join is under way, from connecting until it joined or failed (a reconnect too).
    [[nodiscard]] bool IsJoining() const noexcept { return m_step != Step::kIdle; }
    // Its player is in the bundled character start (no automatic hosting, even after a cancel).
    [[nodiscard]] bool IsCreatingCharacter() const noexcept { return m_creatingCharacter; }
    // Steam lost the host while the join's connection is closed for the character start.
    void CreationHostLost() noexcept;
    // Reconnecting through a Steam tunnel made again: dials it (SteamService); false when not reconnecting.
    bool Redial(const std::string& acEndpoint) noexcept;
    [[nodiscard]] bool IsReconnecting() const noexcept { return m_step == Step::kReconnectWait || m_step == Step::kReconnecting; }
    // The connection asks for the host's save (the transport's join request).
    [[nodiscard]] bool WantsWorld() const noexcept { return m_step == Step::kConnecting || m_step == Step::kChecking; }
    // It brings a character of its own (the join started in a game this player plays).
    [[nodiscard]] bool HasCharacter() const noexcept { return m_brought.has_value(); }
    // This player's key in the host's save: its Steam id in a Steam session, else its own id.
    [[nodiscard]] std::string PlayerKey() const noexcept;
    // The last join's state for whatever shows it: this service's, else the transport's.
    [[nodiscard]] const JoinStatus& GetStatus() const noexcept;

private:
    enum class Step
    {
        kIdle,
        kConnecting,
        kChecking,
        kCreating,
        kReceiving,
        kLoading,
        kPlacing,
        kJoining,
        kReconnectWait, // until the next try
        kReconnecting,
    };

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnAuthentication(const AuthenticationResponse& acResponse) noexcept;
    void OnSaveChunk(const NotifySaveChunk& acChunk) noexcept;
    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnGameLoaded(const GameLoadedEvent&) noexcept;
    void OnGameLoadFailed(const GameLoadFailedEvent&) noexcept;
    // The host loaded a save or the party wiped: the save comes as a join's does, and this game loads it.
    void OnPartyReload(const NotifyPartyReload&) noexcept;

    void To(Step aStep, JoinStatus::State aState) noexcept;
    // The save is loaded: this player's character on its player, then the usual join to the same host.
    void Arrive() noexcept;
    void BeginCreation() noexcept;
    void FinishCreation() noexcept;
    void JoinSession() noexcept;
    void Fail(const char* acpReason) noexcept;
    void Fail(JoinStatus aStatus) noexcept;
    // The session dropped: tries the same host again (SteamService's lobby, else the transport's endpoint).
    void StartReconnect() noexcept;
    void Reconnect() noexcept;
    // A try failed: the next one, or the session is over.
    void Retry() noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    IJoinBridge& m_bridge;
    Step m_step{Step::kIdle};
    JoinStatus m_status;
    std::string m_endpoint;
    std::string m_playerKey; // fixed before creation, through both authentications
    bool m_fromMainMenu{};
    bool m_creatingCharacter{};
    std::optional<PlayerRecord> m_createdCharacter; // survives a failed final join for retry
    bool m_partyReload{};
    std::optional<PlayerRecord> m_joinedCharacter;
    std::optional<PlayerRecord> m_brought; // the character of the game the join started in
    SaveTransfer::Assembly m_save;
    double m_waited{}; // seconds in the current step, or since the last chunk
    bool m_reached{};  // the transport reached the host this attempt
    int m_tries{};     // reconnecting: the tries made
    std::vector<entt::scoped_connection> m_connections;
};
