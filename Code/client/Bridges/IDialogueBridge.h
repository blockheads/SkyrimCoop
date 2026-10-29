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
        std::string Text; // as the menu shows it

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
        /// The engine has built a list and shows it (52857; any thread). Options returned are shown in its place.
        std::function<std::optional<std::vector<Option>>()> List;
        /// An actor's line starts or ends, running its script (any thread): the player character its script runs
        /// for (0: this game's player).
        std::function<uint32_t(uint32_t aSpeakerFormId)> LineFor;
        /// The engine builds a list (any thread): the other players' characters it judges each line for too, as if
        /// each were the player (GetOptionsFor).
        std::function<std::vector<uint32_t>()> Judged;
    };

    virtual ~IDialogueBridge() = default;

    virtual void SetFilters(Filters aFilters) noexcept = 0;

    // Reading the conversation.

    /// The actor the player is talking to (0: none), with the dialogue menu open or not, and whether it is open.
    virtual uint32_t GetSpeaker() const noexcept = 0;
    virtual bool IsMenuOpen() const noexcept = 0;
    /// The options the menu shows, in order.
    virtual std::vector<Option> GetOptions() const noexcept = 0;
    /// A character's own list from the engine's last build, judged for it (the Judged filter): the topics of the
    /// menu's list it gets a line for, in their order, then those only it gets. None: not judged in that build.
    virtual std::optional<std::vector<Option>> GetOptionsFor(uint32_t aCharacterFormId) const noexcept = 0;
    /// Whether the list is a line's choices (35287 sets rootTopicInfo), not the speaker's topics.
    virtual bool IsChoices() const noexcept = 0;
    /// The greeting selected by this game's engine; copies use the host's selected INFO.
    virtual uint32_t GetGreeting() const noexcept = 0;

    // Starting and ending it.

    /// Starts the player's conversation with an actor as talking to it does, without asking the Start filter
    /// (false: it is fighting or dead, or the engine did not start it). Without the menu, the player is not turned
    /// to it and plays on; the conversation runs all the same.
    /// An explicit greeting INFO starts the host-selected line without evaluating a copy's greeting conditions.
    virtual bool Start(uint32_t aActorFormId, bool aShowMenu, uint32_t aGreeting = 0) noexcept = 0;
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

    /// The list becomes these options (topic and line each, as another game's engine built them; no conditions are
    /// tested), and an open menu not showing them already shows them and takes a pick (aChoices: slid in as a line's
    /// choices). False: no conversation, or a personal menu defers the refresh.
    virtual bool ShowList(const std::vector<Option>& acOptions, bool aChoices) noexcept = 0;
    /// Says this topic's line, as clicking its option does: the list's option with it, else one built for it (a line
    /// judged for another player's character). False: no conversation, or no such line.
    virtual bool PickOption(uint32_t aTopic, uint32_t aInfo) noexcept = 0;

    // The highlight (the menu's selected option).

    /// The highlighted option's place in the list (none: no menu, or no list shown).
    virtual std::optional<uint32_t> GetHighlight() const noexcept = 0;
    /// Highlights the option at that place, as the keys would.
    virtual void SetHighlight(uint32_t aIndex) noexcept = 0;
};
