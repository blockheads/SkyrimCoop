// Unequipping everything worn (engine/WornStacks.h) against a model of the engine's inventory list: removing the
// list's first entry copies the second node into the head and frees the second node (16067, 0x1402329A3).
#include <catch2/catch.hpp>

#include <WornStacks.h>

#include <algorithm>
#include <deque>
#include <vector>

namespace
{
struct FakeList
{
    bool Worn = true;
    bool Left = false;
};

struct FakeEntry
{
    int Item = 0;
    std::vector<FakeList*> Lists;
    int CountDelta = 0; // 0: the item is the base container's, its entry only holds the worn list
};

struct Node
{
    FakeEntry* pEntry = nullptr;
    Node* pNext = nullptr;
    bool Freed = false;
};

using Stack = WornStacks::Stack<FakeEntry, FakeList>;

struct FakeChanges
{
    std::deque<Node> Nodes;
    std::deque<FakeEntry> Entries;
    std::deque<FakeList> Lists;
    Node* pHead = nullptr;
    int FreedReads = 0;
    int Unequips = 0;

    void Add(int aItem, int aCountDelta)
    {
        auto& entry = Entries.emplace_back(FakeEntry{aItem, {&Lists.emplace_back()}, aCountDelta});
        auto& node = Nodes.emplace_back(Node{&entry});
        if (!pHead)
            pHead = &node;
        else
        {
            Node* pLast = pHead;
            while (pLast->pNext)
                pLast = pLast->pNext;
            pLast->pNext = &node;
        }
    }

    const Node& Read(const Node* apNode)
    {
        FreedReads += apNode->Freed;
        return *apNode;
    }

    // 16067: the worn extra goes, an empty list goes, and an entry with no list and no count delta leaves the list.
    void Unequip(FakeEntry* apEntry, FakeList* apList)
    {
        ++Unequips;
        apList->Worn = false;
        std::erase(apEntry->Lists, apList);
        if (!apEntry->Lists.empty() || apEntry->CountDelta != 0)
            return;
        if (pHead->pEntry == apEntry)
        {
            if (Node* pSecond = pHead->pNext)
            {
                pHead->pEntry = pSecond->pEntry;
                pHead->pNext = pSecond->pNext;
                pSecond->Freed = true;
            }
            else
                pHead->pEntry = nullptr;
            return;
        }
        for (Node* pPrevious = pHead; pPrevious->pNext; pPrevious = pPrevious->pNext)
            if (pPrevious->pNext->pEntry == apEntry)
            {
                pPrevious->pNext->Freed = true;
                pPrevious->pNext = pPrevious->pNext->pNext;
                return;
            }
    }

    // VisitWornItems (16096) with UnequipAllVisitor: the next node is taken before the visit.
    void EngineUnequipAll()
    {
        for (const Node* pNode = pHead; pNode;)
        {
            const Node& cNode = Read(pNode);
            if (!cNode.pEntry)
                break;
            pNode = cNode.pNext;
            for (FakeList* pList : std::vector(cNode.pEntry->Lists))
                if (pList->Worn)
                    Unequip(cNode.pEntry, pList);
        }
    }

    std::vector<Stack> Worn()
    {
        std::vector<Stack> worn;
        for (const Node* pNode = pHead; pNode && Read(pNode).pEntry; pNode = pNode->pNext)
            for (FakeList* pList : pNode->pEntry->Lists)
                if (pList->Worn)
                    worn.push_back({pNode->pEntry, pList, pList->Left, 1});
        return worn;
    }
};
} // namespace

TEST_CASE("Unequipping everything never follows an inventory node the unequips freed", "[inventory][worn-stacks]")
{
    FakeChanges changes;
    changes.Add(0x13985, 0); // the base container's, worn: its entry goes with the unequip
    changes.Add(0x1397F, 0);
    changes.Add(0x209A5, 1); // added: its entry stays

    SECTION("the engine's walk reads the second node after the first entry's unequip freed it")
    {
        changes.EngineUnequipAll();
        CHECK(changes.FreedReads > 0);
    }
    SECTION("taken first and found again before each unequip: every stack unequipped, nothing freed read")
    {
        const auto cUnequipped = WornStacks::UnequipAll([&] { return changes.Worn(); },
                                                        [&](const Stack& acStack) { changes.Unequip(acStack.pItem, acStack.pList); });
        CHECK(cUnequipped == 3);
        CHECK(changes.Unequips == 3);
        CHECK(changes.Worn().empty());
        CHECK(changes.FreedReads == 0);
    }
}

TEST_CASE("A stack an earlier unequip took is not unequipped again", "[inventory][worn-stacks]")
{
    FakeChanges changes;
    changes.Add(0x13985, 0);
    changes.Add(0x1397F, 0);
    FakeList* pSecond = changes.Entries[1].Lists[0];

    // The first unequip also takes the second stack (its list merged into another and freed, 16067).
    const auto cUnequipped = WornStacks::UnequipAll([&] { return changes.Worn(); },
                                                    [&](const Stack& acStack)
                                                    {
                                                        changes.Unequip(acStack.pItem, acStack.pList);
                                                        if (pSecond->Worn)
                                                            changes.Unequip(&changes.Entries[1], pSecond);
                                                    });
    CHECK(cUnequipped == 1);
    CHECK(changes.Unequips == 2);
    CHECK(changes.FreedReads == 0);
}
