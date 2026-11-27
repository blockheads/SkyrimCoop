// A reference's base change record (engine/BaseChange.h) against a model of the engine's stored-change load:
// TESObjectREFR::LoadGame (19529) gives the reference the recorded base (0x80) by id, then ExtraDataList::LoadGame's
// ExtraLeveledCreature case (12095, 0x140177F9C) destroys the current base if temporary and builds a new one.
#include <catch2/catch.hpp>

#include <BaseChange.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>

namespace
{
struct FakeForms
{
    std::map<uint32_t, int> Npcs; // live temporary NPCs by form id
    std::set<int> Destroyed;
    uint32_t NextId = 0xFF000996;
    int NextNpc = 1;

    uint32_t Build()
    {
        Npcs[NextId] = NextNpc++;
        return NextId++;
    }
    void Destroy(uint32_t aId)
    {
        Destroyed.insert(Npcs.at(aId));
        Npcs.erase(aId); // the dtor takes it off the form map (14627): the id is free for the next temporary
        NextId = aId;
    }
};

struct Reference
{
    int Base = 0;
    uint32_t BaseId = 0;
};

// The reference's stored changes: the base change only if recorded.
struct Stored
{
    std::optional<uint32_t> BaseId;
};

void SetBase(Reference& aRef, Stored& aStored, FakeForms& aForms, uint32_t aId)
{
    aRef.Base = aForms.Npcs.at(aId);
    aRef.BaseId = aId;
    aStored.BaseId = RecordsBaseChange(aId) ? std::optional(aId) : std::nullopt;
}

// 19529 then 12095, on a reference made again from its file (its base the original, 0).
void LoadStored(Reference& aRef, const Stored& acStored, FakeForms& aForms)
{
    if (acStored.BaseId && aForms.Npcs.contains(*acStored.BaseId))
    {
        aRef.BaseId = *acStored.BaseId;
        aRef.Base = aForms.Npcs.at(aRef.BaseId);
    }
    if (aRef.BaseId >= 0xFF000000 && aForms.Npcs.contains(aRef.BaseId))
        aForms.Destroy(aRef.BaseId);
    const uint32_t cBuilt = aForms.Build();
    aRef.BaseId = cBuilt;
    aRef.Base = aForms.Npcs.at(cBuilt);
}
} // namespace

TEST_CASE("A temporary base is never recorded as a reference's base change", "[base-change]")
{
    CHECK_FALSE(RecordsBaseChange(0xFF000996));
    CHECK(RecordsBaseChange(0x23ABE)); // a permanent pick (a leveled item's, ReferenceStates) is
}

TEST_CASE("A copy's rebuilt leveled base is not destroyed under it by its reference's next load", "[base-change]")
{
    FakeForms forms;
    Reference copy, again; // the copy, still held (a held copy, or one the collector has yet to delete), and the
                           // reference the engine makes again when its cell loads
    Stored stored;
    SetBase(copy, stored, forms, forms.Build()); // SkyrimBridge::RelevelActor
    const int cBuilt = copy.Base;

    LoadStored(again, stored, forms);

    CHECK_FALSE(forms.Destroyed.contains(cBuilt));
    CHECK(again.Base != cBuilt);
}
