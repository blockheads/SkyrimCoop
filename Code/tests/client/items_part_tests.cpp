#include <Services/CopyRecords.h>
#include <Structs/ItemsPart.h>
#include <catch2/catch.hpp>

// Items as a record part (docs/design/one-simulator.md, piece 3): a change carries only the stacks that changed, at
// their new counts; a game merges it onto the version just before, else asks for the whole part.
namespace
{
Inventory::Entry Stack(uint32_t aBase, int32_t aCount, bool aWorn = false)
{
    Inventory::Entry stack;
    stack.BaseId = GameId(0, aBase);
    stack.Count = aCount;
    stack.ExtraWorn = aWorn;
    return stack;
}

Inventory Holds(std::vector<Inventory::Entry> aStacks)
{
    Inventory inventory;
    inventory.Entries = std::move(aStacks);
    return inventory;
}

RecordPart Items(uint32_t aVersion, const ItemsPart& acPart)
{
    return {RecordKind::kItems, aVersion, ToPartBytes(acPart)};
}

ItemsPart Read(const RecordPart* apPart)
{
    ItemsPart part;
    REQUIRE(apPart);
    REQUIRE(FromPartBytes(apPart->Bytes, part));
    return part;
}
} // namespace

TEST_CASE("An items change names only the stacks that changed, at their new counts", "[items-part]")
{
    const ItemsPart cHeld = ItemsPart::Of(Holds({Stack(0xF, 10), Stack(0x12EB7, 1, true), Stack(0x1, 2)}));
    const auto cChange = ItemsPart::Change(cHeld, Holds({Stack(0xF, 7), Stack(0x12EB7, 1, true), Stack(0x2, 1)}));
    REQUIRE(cChange);
    CHECK_FALSE(cChange->Full);
    CHECK(cChange->Items.Entries == std::vector{Stack(0xF, 7), Stack(0x2, 1), Stack(0x1, 0)});
    CHECK_FALSE(ItemsPart::Change(cHeld, cHeld.Items)); // nothing changed: nothing to send
}

TEST_CASE("Wearing an item is a change of its stacks", "[items-part]")
{
    const ItemsPart cHeld = ItemsPart::Of(Holds({Stack(0x12EB7, 1)}));
    const auto cChange = ItemsPart::Change(cHeld, Holds({Stack(0x12EB7, 1, true)}));
    REQUIRE(cChange);
    CHECK(ItemsPart::Merged(cHeld, *cChange).Items.Entries == std::vector{Stack(0x12EB7, 1, true)});
}

TEST_CASE("A change merged onto the part before it is the owner's whole part", "[items-part]")
{
    const ItemsPart cHeld = ItemsPart::Of(Holds({Stack(0xF, 10), Stack(0x1, 2)}));
    const Inventory cNow = Holds({Stack(0xF, 3), Stack(0x2, 4)});
    const ItemsPart cMerged = ItemsPart::Merged(cHeld, *ItemsPart::Change(cHeld, cNow));
    CHECK(cMerged.Full);
    CHECK(cMerged == ItemsPart::Of(cNow));
}

TEST_CASE("A copy merges consecutive changes and asks for the whole part after a gap", "[items-part][copy-records]")
{
    CopyRecords records;
    std::vector<std::pair<uint32_t, RecordKind>> asked;
    records.AskWith([&](uint32_t aId, RecordKind aKind) { asked.emplace_back(aId, aKind); });
    const ItemsPart cFirst = ItemsPart::Of(Holds({Stack(0xF, 10)}));
    REQUIRE(records.Take(5, Items(1, cFirst)));

    const ItemsPart cSecond = *ItemsPart::Change(cFirst, Holds({Stack(0xF, 10), Stack(0x2, 1)}));
    REQUIRE(records.Take(5, Items(2, cSecond)));
    CHECK(Read(records.Find(5, RecordKind::kItems)) == ItemsPart::Of(Holds({Stack(0xF, 10), Stack(0x2, 1)})));

    // Version 3 lost: version 4 cannot be merged; the copy keeps 2 and asks.
    const ItemsPart cFourth = *ItemsPart::Change(ItemsPart::Of(Holds({Stack(0xF, 10), Stack(0x2, 1)})), Holds({Stack(0xF, 1)}));
    CHECK_FALSE(records.Take(5, Items(4, cFourth)));
    CHECK(records.Find(5, RecordKind::kItems)->Version == 2);
    CHECK(asked == std::vector<std::pair<uint32_t, RecordKind>>{{5, RecordKind::kItems}});

    // The owner's whole part, sent again under a newer version, is taken whatever came before.
    REQUIRE(records.Take(5, Items(5, ItemsPart::Of(Holds({Stack(0xF, 1)})))));
    CHECK(Read(records.Find(5, RecordKind::kItems)).Items.Entries == std::vector{Stack(0xF, 1)});
}

TEST_CASE("An owner keeps its whole part and sends only the change", "[items-part][copy-records]")
{
    CopyRecords records;
    std::vector<RecordPart> sent;
    records.SendWith([&](uint32_t, const RecordPart& acPart) { sent.push_back(acPart); });
    const ItemsPart cFirst = ItemsPart::Of(Holds({Stack(0xF, 10)}));
    records.Declare(5, RecordKind::kItems, ToPartBytes(cFirst));
    const ItemsPart cChange = *ItemsPart::Change(cFirst, Holds({Stack(0xF, 4)}));
    records.Declare(5, RecordKind::kItems, ToPartBytes(cChange));
    REQUIRE(sent.size() == 2);
    CHECK(Read(&sent[1]) == cChange);
    CHECK(sent[1].Version == 2);
    CHECK(Read(records.Find(5, RecordKind::kItems)) == ItemsPart::Of(Holds({Stack(0xF, 4)})));
    // Asked again: the whole part goes out.
    REQUIRE(records.DeclareAgain(5, RecordKind::kItems));
    CHECK(Read(&sent.back()).Full);
}

TEST_CASE("An enchanted weapon's charge falling is no change of items", "[items-part]")
{
    Inventory::Entry sword = Stack(0x12EB7, 1, true);
    sword.ExtraEnchantId = GameId(0, 0x4B21);
    sword.ExtraCharge = 500.f;
    const ItemsPart cHeld = ItemsPart::Of(Holds({sword}));
    sword.ExtraCharge = 420.f;
    CHECK_FALSE(ItemsPart::Change(cHeld, Holds({sword})));
}

TEST_CASE("A stolen stack is its own stack beside the plain one", "[items-part]")
{
    // Skyrim keeps an item with another's ExtraOwnership in its own list: stolen (inventory-changes.md).
    Inventory::Entry stolen = Stack(0x12EB7, 1);
    stolen.ExtraOwner = GameId(0, 0x1347B);
    const ItemsPart cHeld = ItemsPart::Of(Holds({Stack(0x12EB7, 1)}));
    const auto cChange = ItemsPart::Change(cHeld, Holds({Stack(0x12EB7, 1), stolen}));
    REQUIRE(cChange);
    CHECK(cChange->Items.Entries == std::vector{stolen});
    CHECK_FALSE(ItemsPart::SameStack(Stack(0x12EB7, 1), stolen));
}
