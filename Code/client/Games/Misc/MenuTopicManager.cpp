#include "MenuTopicManager.h"

#include <Forms/TESTopic.h>
#include <Games/Memory.h>
#include <TESObjectREFR.h>
#include <VersionDb.h>

namespace
{
// Holds the manager's critical section for a scope (re-entrant on one thread, as a Windows critical section is).
struct ListLock
{
    explicit ListLock(CRITICAL_SECTION& aSection) noexcept : m_section(aSection) { EnterCriticalSection(&m_section); }
    ~ListLock() { LeaveCriticalSection(&m_section); }
    ListLock(const ListLock&) = delete;
    ListLock& operator=(const ListLock&) = delete;

    CRITICAL_SECTION& m_section;
};
} // namespace

MenuTopicManager* MenuTopicManager::Get() noexcept
{
    POINTER_SKYRIMSE(MenuTopicManager*, s_singleton, 401099);
    return *s_singleton.Get();
}

MenuTopicManager::Entry* MenuTopicManager::FirstOptionNode() noexcept
{
    if (!dialogueList)
        return nullptr;
    // The head node is the list's own; it holds the greeting, not an option, when skipHeadResponse is set.
    Entry* pHead = &dialogueList->entry;
    return skipHeadResponse ? pHead->next : pHead;
}

std::vector<MenuTopicManager::Dialogue*> MenuTopicManager::Options() noexcept
{
    std::vector<Dialogue*> options;
    ListLock lock(criticalSection);
    for (Entry* pNode = FirstOptionNode(); pNode; pNode = pNode->next)
        if (pNode->data)
            options.push_back(pNode->data);
    return options;
}

bool MenuTopicManager::HasOptions() noexcept
{
    using TFirstOption = bool(MenuTopicManager*, bool aSkipHidden);
    POINTER_SKYRIMSE(TFirstOption, s_firstOption, 35267);
    ListLock lock(criticalSection);
    return (s_firstOption.Get())(this, false);
}

void MenuTopicManager::Pick(uint32_t aIndex) noexcept
{
    using TSelectDialogue = void(MenuTopicManager*, uint32_t aIndex);
    using TSaySelected = void(MenuTopicManager*, bool aUnk1, bool aUnk2);
    POINTER_SKYRIMSE(TSelectDialogue, s_selectDialogue, 35269);
    POINTER_SKYRIMSE(TSaySelected, s_saySelected, 35283);
    ListLock lock(criticalSection);
    (s_selectDialogue.Get())(this, aIndex);
    (s_saySelected.Get())(this, false, false);
}

void MenuTopicManager::Clear() noexcept
{
    using TClear = void(MenuTopicManager*);
    POINTER_SKYRIMSE(TClear, s_clear, 35274);
    (s_clear.Get())(this);
}

std::vector<TESTopic*> MenuTopicManager::Choices() noexcept
{
    using TChoices = GameList<TESTopic>*(TESTopicInfo* apInfo);
    POINTER_SKYRIMSE(TChoices, s_choices, 25659);
    std::vector<TESTopic*> topics;
    ListLock lock(criticalSection);
    const GameList<TESTopic>* pChoices = rootTopicInfo ? s_choices.Get()(rootTopicInfo) : nullptr;
    for (const auto* pEntry = pChoices ? &pChoices->entry : nullptr; pEntry; pEntry = pEntry->next)
        if (pEntry->data)
            topics.push_back(pEntry->data);
    return topics;
}

MenuTopicManager::Dialogue* MenuTopicManager::BuildOption(TESTopic* apTopic, TESTopicInfo* apInfo) noexcept
{
    using TConstruct = Dialogue*(Dialogue*, TESQuest*, TESTopic*, TESTopicInfo*, TESObjectREFR*, bool);
    POINTER_SKYRIMSE(TConstruct, s_construct, 35254);
    if (!apTopic || !apInfo)
        return nullptr;
    auto* pOption = static_cast<Dialogue*>(Memory::Allocate(sizeof(Dialogue)));
    if (!pOption)
        return nullptr;
    s_construct.Get()(pOption, apTopic->ownerQuest, apTopic, apInfo, TESObjectREFR::GetByHandle(speaker), false);

    // As AddTopic after the constructor: the first response is said first; an option with none is dropped.
    pOption->currentResponse = &pOption->responses.entry;
    if (!pOption->responses.entry.data)
    {
        Destroy(pOption);
        return nullptr;
    }
    return pOption;
}

void MenuTopicManager::Append(Dialogue* apOption) noexcept
{
    ListLock lock(criticalSection);
    if (!dialogueList)
        return;
    Entry* pLast = &dialogueList->entry;
    while (pLast->next)
        pLast = pLast->next;
    // An empty list holds its first option in the head node.
    if (!pLast->data)
        pLast->data = apOption;
    else
        pLast->next = NewNode(apOption);
}

void MenuTopicManager::SetOptions(const std::vector<Dialogue*>& acOptions) noexcept
{
    std::vector<Dialogue*> dropped;
    {
        ListLock lock(criticalSection);
        if (dialogueList)
            Relink(acOptions, dropped);
        else
            dropped = acOptions;
    }
    // Outside the lock: the engine's destructor.
    for (Dialogue* pOption : dropped)
        Destroy(pOption);
}

void MenuTopicManager::Relink(const std::vector<Dialogue*>& acOptions, std::vector<Dialogue*>& aDropped) noexcept
{
    Entry* pHead = &dialogueList->entry;
    // A picked option is picked again in its new node; a picked greeting head stays as it is.
    const bool cOptionPicked = selectedResponseNode && !(selectedResponseNode == pHead && skipHeadResponse);
    Dialogue* pPicked = cOptionPicked ? selectedResponseNode->data : nullptr;
    if (cOptionPicked)
        selectedResponseNode = nullptr;

    // Empty the list: drop the options no longer wanted, free the nodes but the head (the list's own).
    for (Entry* pNode = FirstOptionNode(); pNode;)
    {
        Entry* pNext = pNode->next;
        if (pNode->data && std::ranges::find(acOptions, pNode->data) == acOptions.end())
            aDropped.push_back(pNode->data);
        if (pNode == pHead)
            *pHead = {};
        else
            Memory::Free(pNode);
        pNode = pNext;
    }
    pHead->next = nullptr;

    // Chain the options after the head, or from it when it is not the greeting's.
    Entry* pLast = skipHeadResponse ? pHead : nullptr;
    for (Dialogue* pOption : acOptions)
    {
        Entry* pNode = pLast ? NewNode(pOption) : pHead;
        if (!pNode)
        {
            aDropped.push_back(pOption);
            continue;
        }
        pNode->data = pOption;
        if (pLast)
            pLast->next = pNode;
        pLast = pNode;
        if (pPicked && pOption == pPicked)
            selectedResponseNode = pNode;
    }
}

MenuTopicManager::Entry* MenuTopicManager::NewNode(Dialogue* apOption) noexcept
{
    // A node as the engine's: 16 bytes on the game's heap.
    auto* pNode = static_cast<Entry*>(Memory::Allocate(sizeof(Entry)));
    if (pNode)
        *pNode = {apOption, nullptr};
    return pNode;
}

void MenuTopicManager::Destroy(Dialogue* apOption) noexcept
{
    using TDestroy = Dialogue*(Dialogue*, bool aFree);
    POINTER_SKYRIMSE(TDestroy, s_destroy, 35319);
    if (apOption)
        (s_destroy.Get())(apOption, true);
}
