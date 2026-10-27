#pragma once

// A game's conversation as the engine runs it (MenuTopicManager, DialogueMenu), behind IDialogueBridge: one speaker
// at a time, a menu that may be open or not, the list the engine builds from a small script of topics, and the
// service's filters where our hooks ask them (DialogueHooks.cpp). Each World has its own.
#include <Bridges/IDialogueBridge.h>

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
    bool ShowList(const std::vector<Option>& acOptions, bool aChoices) noexcept override
    {
        if (!Speaker)
            return false;
        Options = acOptions;
        Choices = aChoices;
        return true;
    }
    bool PickOption(uint32_t aTopic, uint32_t) noexcept override
    {
        if (!Speaker)
            return false;
        for (const auto& cOption : Options)
            if (cOption.Topic == aTopic)
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
    // The engine's list (52857): the List filter may show another in its place.
    void Build(const std::vector<Option>& acOptions)
    {
        Options = acOptions;
        if (m_filters.List)
            if (auto shown = m_filters.List())
                Options = std::move(*shown);
    }

    Filters m_filters;
};
