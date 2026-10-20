#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// The player's conversation in the engine (MenuTopicManager, DialogueMenu). No SKSE types.
class IDialogueBridge
{
public:
    struct Option
    {
        uint32_t Topic{};
        uint32_t Info{};
        uint32_t Quest{};
        std::string Text; // as the menu shows it (read only: an option built here takes its line's)
        bool Added{}; // put in the list here (ShowOptions, PickTopic, held choices), not one the engine built

        bool operator==(const Option&) const noexcept = default;
    };

    /// The service's say where the engine would act on its own. Each is optional; "any thread" ones may be asked
    /// off the main thread.
    struct Filters
    {
        /// Before the player's conversation with an actor starts (aForceGreet: the actor starts it; any thread).
        /// False stops it.
        std::function<bool(uint32_t aActorFormId, bool aForceGreet)> Start;
        /// The player talks to (activates) an actor. True: handled, the engine's activation is skipped.
        std::function<bool(uint32_t aActorFormId)> Talk;
        /// The player clicks an option in the menu (its index). True: handled, the menu's pick is skipped.
        std::function<bool(uint32_t aIndex)> Pick;
        /// Before the engine shows its list (after a line, at the start). True: not shown, the menu keeps what it
        /// shows (a list set here comes later).
        std::function<bool()> List;
        /// A line's choices leave this game's player nothing to say, which ends the conversation. True: it stays on
        /// with the choices held, unlisted, for another player's line, and this game's menu closes.
        std::function<bool()> HoldChoices;
        /// Before an actor's training menu opens (a trainer's line; any thread). True: handled, it does not open.
        std::function<bool(uint32_t aTrainerFormId)> Training;
        /// Before the player gains Speech experience (a persuade's script, barter; any thread). True: it is
        /// someone else's, not added.
        std::function<bool(float aExperience)> SpeechExperience;
    };

    virtual ~IDialogueBridge() = default;

    virtual void SetFilters(Filters aFilters) noexcept = 0;

    // Reading the conversation.

    /// The actor the player is talking to (0: none), with the dialogue menu open or not, and whether it is open.
    virtual uint32_t GetSpeaker() const noexcept = 0;
    virtual bool IsMenuOpen() const noexcept = 0;
    /// The options the menu shows, in order.
    virtual std::vector<Option> GetOptions() const noexcept = 0;
    /// Whether the list is the last line's choices (only they can be said), not the speaker's topics.
    virtual bool ShowsChoices() const noexcept = 0;
    /// The topics of the last line's choices in order, those this game's player gets no line for too (none: the list
    /// is not a line's choices).
    virtual std::vector<Option> GetChoices() const noexcept = 0;
    /// How many lists the engine has shown (52857): one after each line, once the NPC has answered.
    virtual uint32_t GetListsShown() const noexcept = 0;
    /// The line (info) the topic gives this game's player with the speaker, as building the list picks it: its
    /// first whose conditions pass (0: none). None when this game cannot judge it: no conversation, or the topic's
    /// quest is not running here.
    virtual std::optional<uint32_t> GetLine(uint32_t aTopic) const noexcept = 0;

    // Starting and ending it.

    /// Starts the player's conversation with an actor as talking to it does, without asking the Start filter
    /// (false: it is fighting or dead, or the engine did not start it). Without the menu, the player is not turned
    /// to it and plays on; the conversation runs all the same.
    virtual bool Start(uint32_t aActorFormId, bool aShowMenu) noexcept = 0;
    /// Opens the menu on the conversation under way without it, turning the player to the speaker.
    virtual bool ShowMenu() noexcept = 0;
    /// Closes the dialogue menu as leaving it does (its fade-out). Once per menu: a second call restarts the fade.
    virtual void CloseMenu() noexcept = 0;
    /// Whether the menu closing, however it closes, ends the conversation (the engine's way) or leaves it on
    /// without the menu.
    virtual void SetMenuCloseEnds(bool aEnds) noexcept = 0;
    /// Ends the conversation (the speaker's next update), as the menu does when nothing is left to say.
    virtual void End() noexcept = 0;
    /// A conversation whose speaker is gone (a copy deleted while talking: the engine's end then does nothing and
    /// no conversation starts again) is cleared, as ending one does. True: there was one.
    virtual bool ClearOrphaned() noexcept = 0;

    // The list and picks.

    /// The list shows these options (topic and info) in order: the list's own option for a topic and line is kept,
    /// any other is built here. An open menu shows the change and takes a pick again (true: it changed).
    virtual bool ShowOptions(const std::vector<Option>& acOptions) noexcept = 0;
    /// An open menu shows its list again and takes a pick, as after a line (a click that was not said).
    virtual void Redraw() noexcept = 0;
    /// Says the option at this index in the list, as clicking it in the menu does.
    virtual void Pick(uint32_t aIndex) noexcept = 0;
    /// Says the topic with this info, as picking it does: the list's option for it, or one built for it; an info
    /// this game cannot say for the topic, the list's own option for it. No conditions are tested (false: no
    /// conversation, or nothing to say for the topic).
    virtual bool PickTopic(uint32_t aTopic, uint32_t aInfo) noexcept = 0;
    /// This line was said now, as saying it marks it: said (a say-once line is not offered again) and heard by the
    /// player (one with reset hours is not offered again until they pass).
    virtual void MarkSaid(uint32_t aInfo) noexcept = 0;

    // What a line leads to on the picker's game.

    /// Opens the actor's training menu for the player, as its trainer's line does.
    virtual void OpenTraining(uint32_t aTrainerFormId) noexcept = 0;
    /// The player gains Speech experience as earning it does (this game's bonuses apply).
    virtual void GainSpeechExperience(float aExperience) noexcept = 0;
};
