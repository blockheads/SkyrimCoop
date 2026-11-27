#pragma once

#include <network/ENetInterface.h>
#include <Messages/Message.h>
#include <Opcodes.h>
#include <characters/HostCharacters.h>
#include <core/HostChat.h>
#include <travel/HostLeash.h>
#include <time/HostClock.h>
#include <combat/HostCombat.h>
#include <tools/HostDevTools.h>
#include <death/HostDeath.h>
#include <dialogue/HostDialogue.h>
#include <game/HostGame.h>
#include <animation/HostGraph.h>
#include <followers/HostFollowers.h>
#include <physics/HostBodies.h>
#include <items/HostItems.h>
#include <magic/HostMagic.h>
#include <travel/HostMap.h>
#include <world/HostObjects.h>
#include <scripts/HostPlayerNatives.h>
#include <core/HostPlayers.h>
#include <characters/HostRecords.h>
#include <core/HostRouter.h>
#include <join/HostSaveTransfer.h>
#include <join/HostSession.h>
#include <scripts/HostStory.h>
#include <time/HostTimeScale.h>
#include <time/HostWait.h>
#include <crime/HostCrime.h>
#include <items/HostPartyItems.h>
#include <time/HostWeather.h>
#include <crime/HostEye.h>
#include <world/HostWorldState.h>

#include <optional>
#include <vector>
#include <functional>
#include <string_view>

#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>


struct HostNetwork;

/**
 * @brief The host's side of the session: builds the host's parts and decides who may send at all
 * (docs/design/host-authority-migration.md).
 *
 * Every message the host's network (HostNetwork) receives comes here; one with no rule is dropped. The host is a
 * player like the others: what it sends comes through here in-process.
 *
 * It holds no rule of its own. Each part (a Host* file) registers its rules with the router (HostRouter) and keeps
 * its own state. What stays here is what needs every part at once: the order a joiner is told things, the order
 * the parts hear a player left, and each frame's update.
 */
struct HostAuthority
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Installs itself on aNetwork (front door, leaving players), which calls into it from then on:
    // the network goes first.
    HostAuthority(HostNetwork& aNetwork, HostGame aGame) noexcept;

    NO_COPY_MOVE(HostAuthority);

    // True when the message was handled here.
    bool Handle(const RequestMessage& acMessage, ConnectionId_t aSender) noexcept;
    // Every frame: the session's tick (milliseconds) and the game time it took (seconds, none while paused).
    // aFrameTick: the frame clock (World::GetFrameTick), what physics frames are stamped with (S10); 0: aTick.
    void Update(uint64_t aTick, double aDelta, uint64_t aFrameTick = 0) noexcept;
    // A party reload (HostServiceReload.cpp): the selected save to every guest, delivered once every guest has it and
    // has left to load it; a failed reload's reason to every player.
    void SendCheckpoint(const std::string& acSave) noexcept;
    // The host's game made a save (its image's id): ahead to every guest, for a later load of it.
    void OfferSave(const std::string& acSave) noexcept;
    [[nodiscard]] bool HasGuests() const noexcept { return m_players.Count() > 1; }
    [[nodiscard]] bool CheckpointDelivered() const noexcept { return m_transfer.Empty() && m_players.Count() <= 1; }
    void FailReload(const std::string& acError) noexcept { m_death.FailReload(acError); }
    // This game's engine renamed an actor another game owns (HostService): its owner names its actor, and its name part
    // carries that on.
    void NameActor(uint32_t aSessionId, const std::string& acName) const noexcept;

    // A player's own character is in the world: everything it must know about the session, from every store.
    void PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterSessionId) noexcept;
    // What a joiner gets, in order (one-simulator.md piece 9): every store holding session state is on it.
    struct JoinerStore
    {
        std::string_view Name;
        std::function<void(ConnectionId_t aPlayer, uint32_t aCharacterSessionId)> Send;
    };
    [[nodiscard]] const std::vector<JoinerStore>& JoinerStores() const noexcept { return m_joinerStores; }
    // A player's character is in the host's world, and the record a character arrived with (its AI state counts for
    // death).
    void PlayerBodyEntered(ConnectionId_t aPlayer, uint32_t aId) noexcept { m_death.Entered(aPlayer, aId); }
    void ActorAnnounced(uint32_t aId, const RecordParts& acRecord) noexcept;
    // A character left the session: its record goes.
    void ActorRemoved(uint32_t aId) noexcept { m_records.Forget(aId); }
    // The host's game took back a claimed actor given back (HostClaims::Returning ends).
    void ActorTaken(uint32_t aId) noexcept { m_characters.Claims().Taken(aId); }
    // A character's record as its owner last declared it, for a spawn of it.
    [[nodiscard]] RecordParts RecordOf(uint32_t aId) const noexcept { return m_records.PartsOf(aId); }
    // The host removes a player from the session.
    void Kick(uint32_t aPlayerId) noexcept;
    // The host's game set its difficulty (DifficultyChangeEvent).
    void DifficultyChanged(uint32_t aDifficulty) const noexcept;
    // The host's game is saving: the guests' records take their characters' places (HostRecords).
    void Saving() const noexcept { m_records.OnSaving(); }
    // A session id for one of the host's actors (HostCharacters).
    [[nodiscard]] uint32_t NewCharacterId() noexcept { return m_characters.NewCharacterId(); }

private:
    using Delivery = HostRouter::Delivery;

    // Whether aSender may send aOpcode at all (HostNetwork asks before decoding).
    [[nodiscard]] bool Admits(RequestOpcode aOpcode, ConnectionId_t aSender) const noexcept;
    [[nodiscard]] std::vector<ConnectionId_t> Guests() const noexcept;

    HostNetwork& m_network;
    HostGame m_game;
    // The parts, after the rules they register into, and the players first: the others read them.
    HostPlayers m_players;
    HostRouter m_router;
    HostChat m_chat;
    HostSaveTransfer m_transfer;
    HostSession m_session;
    HostCharacters m_characters;
    HostClock m_clock;
    HostWait m_wait;
    HostCrime m_crime;
    HostPartyItems m_partyItems;
    HostWeather m_weather;
    HostEye m_eye;
    HostWorldState m_worldState;
    HostItems m_items;
    HostBodies m_bodies;
    HostTimeScale m_timeScale;
    HostDialogue m_dialogue;
    HostPlayerNatives m_playerNatives;
    HostRecords m_records;
    HostStory m_story;
    HostDeath m_death;
    HostLeash m_leash;
    HostFollowers m_followers;
    HostDevTools m_devTools;
    HostObjects m_objects;
    HostCombat m_combat;
    HostMagic m_magic;
    HostGraph m_graph;
    HostMap m_map;
    std::vector<JoinerStore> m_joinerStores;
    bool m_reloading{};
};
