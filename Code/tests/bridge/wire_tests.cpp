// The bytes of every message the bridge relays, request and notification: peers built from other
// commits must keep reading them. A request and its notification carry the same payload, so the
// notification here is read back from the request's own bytes.
#include <catch2/catch.hpp>

#include <Messages/AddTargetRequest.h>
#include <Messages/DialogueRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/MountRequest.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyMount.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/SpellCastRequest.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/SyncExperienceRequest.h>

#include <map>
#include <string>

namespace
{
template <class TMessage> std::string Hex(const TMessage& acMessage)
{
    Buffer buffer(1 << 14);
    Buffer::Writer writer(&buffer);
    acMessage.Serialize(writer);
    std::string hex;
    for (size_t i = 0; i < writer.Size(); ++i)
        hex += fmt::format("{:02x}", buffer.GetWriteData()[i]);
    return hex;
}

// The notification read from the request's payload (its bytes after the opcode).
template <class TNotify, class TRequest> TNotify FromRequestBytes(const TRequest& acRequest)
{
    Buffer buffer(1 << 14);
    Buffer::Writer writer(&buffer);
    acRequest.Serialize(writer);
    ViewBuffer view(buffer.GetWriteData() + 1, writer.Size() - 1);
    Buffer::Reader reader(&view);
    TNotify notify;
    notify.DeserializeRaw(reader);
    return notify;
}

Inventory SomeEquipment()
{
    Inventory inventory;
    Inventory::Entry entry;
    entry.BaseId = GameId(3, 0x12EB7);
    entry.Count = 2;
    entry.ExtraHealth = 1.5f;
    entry.ExtraWorn = true;
    inventory.Entries.push_back(entry);
    return inventory;
}

// Every relayed request with distinct, non-default values in each field.
std::map<std::string, std::pair<std::string, std::string>> Encode()
{
    std::map<std::string, std::pair<std::string, std::string>> out;
    auto add = [&]<class TNotify>(const char* acpName, const auto& acRequest, TNotify*) {
        out[acpName] = {Hex(acRequest), Hex(FromRequestBytes<TNotify>(acRequest))};
    };

    {
        RequestEquipmentSnapshot m;
        m.ServerId = 0x123456;
        m.Equipment = SomeEquipment();
        add("EquipmentSnapshot", m, (NotifyEquipmentSnapshot*)nullptr);
    }
    {
        DrawWeaponRequest m;
        m.Id = 0x2345;
        m.IsWeaponDrawn = true;
        add("DrawWeapon", m, (NotifyDrawWeapon*)nullptr);
    }
    {
        RequestActorValueChanges m;
        m.Id = 0x3456;
        m.Values[24] = 51.25f;
        m.Values[25] = -3.5f;
        add("ActorValues", m, (NotifyActorValueChanges*)nullptr);
    }
    {
        RequestActorMaxValueChanges m;
        m.Id = 0x4567;
        m.Values[26] = 250.f;
        add("ActorMaxValues", m, (NotifyActorMaxValueChanges*)nullptr);
    }
    {
        RequestDeathStateChange m;
        m.Id = 0x5678;
        m.IsDead = true;
        add("DeathState", m, (NotifyDeathStateChange*)nullptr);
    }
    {
        RequestHealthChangeBroadcast m;
        m.Id = 0x6789;
        m.DeltaHealth = -12.75f;
        m.HasAttacker = true;
        m.AttackerId = 0x789A;
        add("HealthChange", m, (NotifyHealthChangeBroadcast*)nullptr);
    }
    {
        SpellCastRequest m;
        m.CasterId = 0x89AB;
        m.SpellFormId = GameId(1, 0x12FCD);
        m.CastingSource = 2;
        m.IsDualCasting = true;
        m.DesiredTarget = 0x9ABC;
        add("SpellCast", m, (NotifySpellCast*)nullptr);
    }
    {
        InterruptCastRequest m;
        m.CasterId = 0xABCD;
        m.CastingSource = 3;
        add("InterruptCast", m, (NotifyInterruptCast*)nullptr);
    }
    {
        ProjectileLaunchRequest m;
        m.ShooterID = 0xBCDE;
        m.OriginX = 1.25f;
        m.OriginY = -2.5f;
        m.OriginZ = 300.75f;
        m.ProjectileBaseID = GameId(0, 0x10FBED);
        m.WeaponID = GameId(0, 0x12EB7);
        m.AmmoID = GameId(0, 0x1397D);
        m.ZAngle = 0.5f;
        m.XAngle = -0.25f;
        m.YAngle = 0.125f;
        m.ParentCellID = GameId(0, 0x3C);
        m.SpellID = GameId(0, 0x12FCD);
        m.CastingSource = 1;
        m.Area = 4;
        m.Power = 0.75f;
        m.Scale = 1.5f;
        m.AlwaysHit = true;
        m.NoDamageOutsideCombat = false;
        m.AutoAim = true;
        m.DeferInitialization = false;
        m.ForceConeOfFire = true;
        m.UnkBool1 = false;
        m.UnkBool2 = true;
        add("ProjectileLaunch", m, (NotifyProjectileLaunch*)nullptr);
    }
    {
        AddTargetRequest m;
        m.TargetId = 0xCDEF;
        m.CasterId = 0xDEF0;
        m.SpellId = GameId(1, 0x12FCD);
        m.EffectId = GameId(1, 0x12FCE);
        m.Magnitude = 25.5f;
        m.IsDualCasting = true;
        m.ApplyHealPerkBonus = true;
        m.ApplyStaminaPerkBonus = false;
        add("AddTarget", m, (NotifyAddTarget*)nullptr);
    }
    {
        RemoveSpellRequest m;
        m.TargetId = 0xEF01;
        m.SpellId = GameId(2, 0x12FCD);
        add("RemoveSpell", m, (NotifyRemoveSpell*)nullptr);
    }
    {
        MountRequest m;
        m.RiderId = 0xF012;
        m.MountId = 0x0123;
        add("Mount", m, (NotifyMount*)nullptr);
    }
    {
        NewPackageRequest m;
        m.ActorId = 0x1234;
        m.PackageId = GameId(0, 0x5432);
        add("NewPackage", m, (NotifyNewPackage*)nullptr);
    }
    {
        DialogueRequest m;
        m.ServerId = 0x2345;
        m.SoundFilename = "Sound\\Voice\\Skyrim.esm\\FemaleNord\\hello.fuz";
        add("Dialogue", m, (NotifyDialogue*)nullptr);
    }
    {
        SubtitleRequest m;
        m.ServerId = 0x3456;
        m.Text = "Let me guess...";
        m.TopicFormId = 0x4567;
        add("Subtitle", m, (NotifySubtitle*)nullptr);
    }
    {
        SyncExperienceRequest m;
        m.Experience = 12.5f;
        add("SyncExperience", m, (NotifySyncExperience*)nullptr);
    }
    return out;
}
} // namespace

TEST_CASE("Relayed messages keep their bytes", "[Wire]")
{
    // {request, notification}, recorded from the messages before they shared payload structs.
    const std::map<std::string, std::pair<std::string, std::string>> cRecorded{
        {"ActorMaxValues", {"0de78a01011a00007a43", "0ee78a01011a00007a43"}},
        {"ActorValues", {"0cd668021800004d4219000060c0", "0dd668021800004d4219000060c0"}},
        {"AddTarget", {"17ef9b03f0bd03cddf0401cedf04010000cc4103", "19ef9b03f0bd03cddf0401cedf04010000cc4103"}},
        {"DeathState", {"12f8ac0101", "14f8ac0101"}},
        {"Dialogue", {"24c5462b536f756e645c566f6963655c536b7972696d2e65736d5c46656d616c654e6f72645c68656c6c6f2e66757a", "27c5462b536f756e645c566f6963655c536b7972696d2e65736d5c46656d616c654e6f72645c68656c6c6f2e66757a"}},
        {"DrawWeapon", {"1bc54601", "1dc54601"}},
        {"EquipmentSnapshot", {"30d6e84801b7dd04030200000000000000000000c03f0000000004", "37d6e84801b7dd04030200000000000000000000c03f0000000004"}},
        {"HealthChange", {"0e89cf0100004cc135e30300", "0f89cf0100004cc135e30300"}},
        {"InterruptCast", {"16cdd70203", "18cdd70203"}},
        {"Mount", {"1c92e003a302", "1e92e003a302"}},
        {"NewPackage", {"1db424b2a80100", "1fb424b2a80100"}},
        {"ProjectileLaunch", {"19def9020000a03f000020c000609643edf74300b7dd0400fdf204000000003f000080be0000003e3c00cddf040001040000403f0000c03f55", "1bdef9020000a03f000020c000609643edf74300b7dd0400fdf204000000003f000080be0000003e3c00cddf040001040000403f0000c03f55"}},
        {"RemoveSpell", {"1881de03cddf0402", "1a81de03cddf0402"}},
        {"SpellCast", {"15ab9302cddf040102796b0500", "17ab9302cddf040102796b0500"}},
        {"Subtitle", {"25d6680f4c6574206d652067756573732e2e2ee78a01", "28d6680f4c6574206d652067756573732e2e2ee78a01"}},
        {"SyncExperience", {"1f00004841", "2100004841"}},
    };

    const auto cEncoded = Encode();
    REQUIRE(cEncoded.size() == cRecorded.size());
    for (const auto& [cName, cBytes] : cEncoded)
    {
        INFO(cName);
        REQUIRE(cRecorded.count(cName) == 1);
        CHECK(cBytes.first == cRecorded.at(cName).first);
        CHECK(cBytes.second == cRecorded.at(cName).second);
    }
}
