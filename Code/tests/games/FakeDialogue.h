#pragma once

// A game's conversation as the engine runs it (MenuTopicManager, DialogueMenu), behind IDialogueBridge: one speaker
// at a time, a menu that may be open or not, the list the engine builds from a small script of topics, and the
// service's filters where our hooks ask them (DialogueHooks.cpp). Each World has its own.
#include <Bridges/IDialogueBridge.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

struct FakeDialogue final : IDialogueBridge
{
    // What saying a topic leads to: the next list (empty: the conversation ends, a goodbye).
    std::map<uint32_t, std::vector<Option>> After;
    // The speaker's topics when a conversation starts.
    std::vector<Option> Greeting;
    uint32_t GreetingInfo{};
    bool RequiresGreeting{}; // native arrest greetings depend on alarm AI absent on copies
    uint32_t StartedGreeting{};
    std::function<void(const Option&)> OnSaid;
    uint32_t LastLineFor = 0;
    // A line's conditions (by topic and info): the players it is said to (their character's form id here; 0x14 this
    // game's player). A topic's first line that passes is the player's, as TESTopic 25544 picks; none listed: everyone.
    std::map<std::pair<uint32_t, uint32_t>, std::function<bool(uint32_t aPlayer)>> Needs;
    // Actors that cannot talk (dead, fighting): Start refuses them.
    std::function<bool(uint32_t aActorFormId)> CanTalk = [](uint32_t) { return true; };

    uint32_t Speaker = 0;
    bool MenuOpen = false;
    bool MenuCloseEnds = true;
    std::vector<Option> Options;
    bool Choices = false;
    std::vector<Option> Said; // every option said here, in order

    // This game's player talks to an actor (activates it), as the engine with our hooks: Talk first, then Start.
    void PlayerTalksTo(uint32_t aActorFormId)
    {
        if (m_filters.Talk && m_filters.Talk(aActorFormId))
            return;
        if (m_filters.Start && !m_filters.Start(aActorFormId, false))
            return;
        Start(aActorFormId, true);
    }
    void NpcGreets(uint32_t aActorFormId)
    {
        if (!m_filters.Start || m_filters.Start(aActorFormId, true))
            Start(aActorFormId, true);
    }
    void RebuildOptions(const std::vector<Option>& aOptions)
    {
        Build(aOptions);
    }
    // This game's player clicks the menu's option: the Pick filter first, else the engine says it.
    void PlayerPicks(uint32_t aIndex)
    {
        if (!MenuOpen || aIndex >= Options.size())
            return;
        if (m_filters.Pick && m_filters.Pick(aIndex))
            return;
        Say(Options[aIndex]);
    }
    // This game's player leaves the menu.
    void PlayerLeaves()
    {
        CloseMenu();
    }

    void SetFilters(Filters aFilters) noexcept override
    {
        m_filters = std::move(aFilters);
    }
    uint32_t GetSpeaker() const noexcept override
    {
        return Speaker;
    }
    bool IsMenuOpen() const noexcept override
    {
        return MenuOpen;
    }
    std::vector<Option> GetOptions() const noexcept override
    {
        return Options;
    }
    std::optional<std::vector<Option>> GetOptionsFor(uint32_t aCharacterFormId) const noexcept override
    {
        const auto it = m_judged.find(aCharacterFormId);
        return it != m_judged.end() ? std::optional(it->second) : std::nullopt;
    }
    bool IsChoices() const noexcept override
    {
        return Choices;
    }
    uint32_t GetGreeting() const noexcept override
    {
        return GreetingInfo;
    }
    bool Start(uint32_t aActorFormId, bool aShowMenu, uint32_t aGreeting = 0) noexcept override
    {
        if (!aActorFormId || !CanTalk(aActorFormId))
            return false;
        if (RequiresGreeting && !aGreeting)
            return true; // native start accepted, but no eligible local greeting opens a menu
        StartedGreeting = aGreeting;
        Speaker = aActorFormId;
        MenuOpen = aShowMenu;
        Choices = false;
        Build(Greeting);
        return true;
    }
    bool ShowMenu() noexcept override
    {
        MenuOpen = Speaker != 0;
        return MenuOpen;
    }
    void CloseMenu() noexcept override
    {
        MenuOpen = false;
        if (MenuCloseEnds)
            End();
    }
    void SetMenuCloseEnds(bool aEnds) noexcept override
    {
        MenuCloseEnds = aEnds;
    }
    void End() noexcept override
    {
        Speaker = 0;
        MenuOpen = false;
        Options.clear();
    }
    bool ClearOrphaned() noexcept override
    {
        return false;
    }
    bool PlayerMenuOpen = false;
    bool ShowList(const std::vector<Option>& acOptions, bool aChoices) noexcept override
    {
        if (!Speaker || PlayerMenuOpen)
            return false;
        Options = acOptions;
        Choices = aChoices;
        return true;
    }
    // The listed option with this line, else that line built (as DialogueBridge::PickOption).
    bool PickOption(uint32_t aTopic, uint32_t aInfo) noexcept override
    {
        if (!Speaker)
            return false;
        for (const auto* pList : {&Options, &m_source})
            for (const auto& cOption : *pList)
                if (cOption.Topic == aTopic && cOption.Info == aInfo)
                {
                    Say(cOption);
                    return true;
                }
        return false;
    }

  private:
    // The engine says the option, then builds the next list or ends.
    void Say(const Option& acOption)
    {
        Said.push_back(acOption);
        LastLineFor = m_filters.LineFor ? m_filters.LineFor(Speaker) : 0;
        if (OnSaid)
            OnSaid(Said.back());
        const auto cNext = After.find(acOption.Topic);
        if (cNext == After.end() || cNext->second.empty())
        {
            End();
            return;
        }
        Build(cNext->second);
    }
    // A player's lines from the topics: each topic's first line that passes for it.
    std::vector<Option> LinesFor(const std::vector<Option>& acTopics, uint32_t aPlayer) const
    {
        std::vector<Option> lines;
        for (const auto& cOption : acTopics)
        {
            const auto cNeed = Needs.find({cOption.Topic, cOption.Info});
            const bool cTaken = std::ranges::any_of(lines, [&](const Option& acLine) { return acLine.Topic == cOption.Topic; });
            if (!cTaken && (cNeed == Needs.end() || cNeed->second(aPlayer)))
                lines.push_back(cOption);
        }
        return lines;
    }
    // The engine's list (35287, then 52857): this game's player's, and each judged character's (the menu's topics it
    // has a line for, in the menu's order, then those only it has: DialogueBridge's HookBuildList); the List filter may
    // show another in its place.
    void Build(const std::vector<Option>& acOptions)
    {
        m_source = acOptions;
        Options = LinesFor(acOptions, 0x14);
        m_judged.clear();
        for (const uint32_t cCharacter : m_filters.Judged ? m_filters.Judged() : std::vector<uint32_t>{})
        {
            const std::vector<Option> cOwn = LinesFor(acOptions, cCharacter);
            auto& list = m_judged[cCharacter];
            for (const auto& cListed : Options)
                if (const auto it = std::ranges::find(cOwn, cListed.Topic, &Option::Topic); it != cOwn.end())
                    list.push_back(*it);
            for (const auto& cLine : cOwn)
                if (std::ranges::find(Options, cLine.Topic, &Option::Topic) == Options.end())
                    list.push_back(cLine);
        }
        if (m_filters.List)
            if (auto shown = m_filters.List())
                Options = std::move(*shown);
    }

    Filters m_filters;
    std::vector<Option> m_source; // the topics of the last build
    std::map<uint32_t, std::vector<Option>> m_judged;
};
