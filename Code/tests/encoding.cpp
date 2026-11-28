#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Wire/Buffer.h>
#include <Wire/Serialization.h>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>


#include <catch2/catch.hpp>

#include <Messages/EnterCellRequest.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestMessageFactory.h>
#include <Messages/RelayedMessages.h>
#include <Messages/Delivery.h>
#include <Messages/NotifyMessageFactory.h>
#include <Messages/NotifyTimeSettings.h>
#include <Messages/WorldState.h>
#include <Messages/DroppedItems.h>
#include <Structs/ActorAiState.h>
#include <Structs/ActorStatePart.h>
#include <Structs/RecordPart.h>
#include <Structs/RidePart.h>
#include <Structs/CastPart.h>
#include <Structs/RagdollPart.h>
#include <Messages/ActivateRequest.h>
#include <Structs/SystemQuestState.h>
#include <Structs/GraphOps.h>

#include <bit>
#include <numbers>
#include <limits>
#include <array>

TEST_CASE("Variable integers reject truncated and overflowing input", "[encoding.varint]")
{
    for (const uint64_t cValue : {uint64_t(0), uint64_t(127), uint64_t(128), std::numeric_limits<uint64_t>::max()})
    {
        Wire::Buffer buffer;
        Wire::Buffer::Writer writer(&buffer);
        Wire::Serialization::WriteVarInt(writer, cValue);
        Wire::Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
        uint64_t value = 0;
        REQUIRE(Wire::Serialization::TryReadVarInt(reader, value));
        CHECK(value == cValue);
        for (size_t size = 0; size < writer.Size(); ++size)
        {
            Wire::Buffer::Reader truncated(std::span(buffer.GetData(), size));
            CHECK_FALSE(Wire::Serialization::TryReadVarInt(truncated, value));
        }
    }
    for (const uint8_t cLast : {uint8_t(2), uint8_t(0x80)})
    {
        std::array<uint8_t, 10> bytes;
        bytes.fill(0x80);
        bytes.back() = cLast;
        Wire::Buffer::Reader reader{std::span(bytes)};
        uint64_t value = 0;
        CHECK_FALSE(Wire::Serialization::TryReadVarInt(reader, value));
    }
}

TEST_CASE("A caught pickpocket names only its victim", "[encoding.crime]")
{
    // AttemptPickpocket (40654) raises PickpocketAlarm (37423) with no item and count 0.
    CrimeRequest request;
    request.Sequence = 7;
    request.Action = CrimeAction::Pickpocket;
    request.TargetActor = 42;
    CHECK(request.IsValid());
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    request.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = RequestMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    const auto& received = static_cast<const CrimeRequest&>(*decoded);
    CHECK(received.IsValid());
    CHECK(received.Action == CrimeAction::Pickpocket);
    CHECK(received.TargetActor == 42);
    CHECK(!received.Item);
    CHECK(received.Count == 0);
    CrimeRequest nobody = request;
    nobody.TargetActor = 0;
    CHECK(!nobody.IsValid());
    CrimeRequest theft = request;
    theft.Action = CrimeAction::Theft;
    CHECK(!theft.IsValid()); // a theft still names what it took
}

TEST_CASE("Crime messages retain actions and absolute faction buckets", "[encoding.crime]")
{
    CrimeRequest request;
    request.Sequence = 1234;
    request.Action = CrimeAction::ContainerTheft;
    request.Target = GameId(0xFE, 0x100812);
    request.TargetActor = 987;
    request.Item = GameId(2, 0xABC);
    request.Owner = GameId(0, 0x13479);
    request.Count = 81;
    CrimeItem item;
    item.Item.BaseId = GameId(0, 0x12EB7);
    item.Item.Count = 3;
    item.Item.ExtraHealth = 1.2f;
    item.Owners = {{GameId(0, 0x13477), 1}, {GameId{}, 2}};
    item.Taking = true;
    request.Items.push_back(item);
    request.GoToJail = request.RemoveStolenItems = request.AllowWarning = true;
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    request.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = RequestMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == CrimeRequest::Opcode);
    const auto& received = static_cast<const CrimeRequest&>(*decoded);
    CHECK(received.IsValid());
    CHECK(received.Sequence == request.Sequence);
    CHECK(received.Action == request.Action);
    CHECK(received.Target == request.Target);
    CHECK(received.TargetActor == request.TargetActor);
    CHECK(received.Item == request.Item);
    CHECK(received.Owner == request.Owner);
    CHECK(received.Count == request.Count);
    CHECK(received.GoToJail);
    CHECK(received.RemoveStolenItems);
    CHECK(received.AllowWarning);
    REQUIRE(received.Items.size() == 1);
    CHECK(received.Items[0].Item == item.Item);
    CHECK(received.Items[0].Taking);
    REQUIRE(received.Items[0].Owners.size() == 2);
    CHECK(received.Items[0].Owners[0].Owner == item.Owners[0].Owner);
    CHECK(received.Items[0].Owners[1].Count == 2);
}

TEST_CASE("Crime requests initialize omitted IDs and reject invalid ownership stacks", "[encoding.crime]")
{
    CrimeRequest request;
    CHECK_FALSE(request.Target);
    CHECK_FALSE(request.Item);
    request.Sequence = 1;
    request.Action = CrimeAction::ContainerTheft;
    request.Target = GameId(0, 0x1234);
    CrimeItem line;
    line.Item.BaseId = GameId(0, 0x64B3D);
    line.Item.Count = 7;
    line.Owners = {{GameId(0, 0x13479), 7}};
    request.Items.push_back(line);
    SECTION("A real stack quantity survives the wire")
    {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        request.SerializeRaw(writer);
        Buffer::Reader reader(&buffer);
        CrimeRequest decoded;
        decoded.DeserializeRaw(reader);
        REQUIRE(decoded.IsValid());
        REQUIRE(decoded.Items.size() == 1);
        REQUIRE(decoded.Items[0].Owners.size() == 1);
        CHECK(decoded.Items[0].Owners[0].Count == 7);
    }
    SECTION("Quantities cannot wrap into the native signed 16-bit count")
    {
        for (auto count : {0u, 0x8000u, 0x10001u, 0xFFFFFFFFu})
        {
            request.Items[0].Owners[0].Count = count;
            Buffer buffer;
            Buffer::Writer writer(&buffer);
            request.SerializeRaw(writer);
            Buffer::Reader reader(&buffer);
            CrimeRequest decoded;
            decoded.DeserializeRaw(reader);
            CHECK_FALSE(decoded.IsValid());
            CHECK(reader.Failed());
        }
    }
    SECTION("The entry budget covers nested stacks as well as item lines")
    {
        request.Items[0].Owners.assign(CrimeRequest::kMaxEntries, {GameId{}, 1});
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        request.SerializeRaw(writer);
        Buffer::Reader reader(&buffer);
        CrimeRequest decoded;
        decoded.DeserializeRaw(reader);
        CHECK(reader.Failed());
    }
}

using namespace Wire;

TEST_CASE("Buffer", "[encoding.buffer]")
{
    SECTION("A writer grows its buffer")
    {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        Serialization::WriteString(writer, std::string(3000, 'a'));
        Buffer::Reader reader(&buffer);
        CHECK(Serialization::ReadString(reader) == std::string(3000, 'a'));
    }

    SECTION("A writer into fixed bytes stops at their end")
    {
        uint8_t bytes[4]{};
        Buffer::Writer writer{std::span(bytes)};
        CHECK(writer.WriteBits(0xABCDEF, 24));
        CHECK_FALSE(writer.WriteBits(0x1234, 16));
        CHECK(writer.Size() == 3);
    }

    SECTION("A reader stops at the end of its bytes")
    {
        const uint8_t bytes[2]{0x34, 0x12};
        Buffer::Reader reader{std::span(bytes)};
        uint64_t value = 0;
        CHECK(reader.ReadBits(value, 16));
        CHECK(value == 0x1234);
        CHECK(reader.Eof());
        CHECK_FALSE(reader.ReadBits(value, 1));
    }
}

TEST_CASE("Static structures", "[encoding.static]")
{
    GIVEN("Vector3_NetQuantize keeps a walking copy's place to an eighth of a unit")
    {
        // Whole units made a walking copy step unevenly: 0 for a while, then a unit at once (smooth-movement baseline).
        for (const glm::vec3 cAt : {glm::vec3{142.56f, -45687.7f, -142.37f}, glm::vec3{-0.3f, 0.62f, 7123.88f},
                                    glm::vec3{250000.12f, -250000.9f, -30000.6f}})
        {
            Vector3_NetQuantize sent, received;
            sent = cAt;
            Buffer buff(100);
            Buffer::Writer writer(&buff);
            sent.Serialize(writer);
            Buffer::Reader reader(&buff);
            received.Deserialize(reader);
            for (int axis = 0; axis < 3; ++axis)
                CHECK(std::abs(received[axis] - cAt[axis]) <= 0.125f);
            CHECK(writer.Size() == 8);
        }
    }

    GIVEN("Rotator2_NetQuantize decoded and encoded again (fuzzer: a relayed rotation lost a step)")
    {
        for (uint32_t step = 0; step <= 0xFFFF; ++step)
        {
            Rotator2_NetQuantize rotation;
            rotation.Unpack(step | (0xFFFF - step) << 16);
            REQUIRE(rotation.Pack() == (step | (0xFFFF - step) << 16));
        }
    }
}

TEST_CASE("Differential structures", "[encoding.differential]")
{
    GIVEN("Full ActionEvent")
    {
        ActionEvent sendAction, recvAction;

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.StateAfter1 = 6547 | 0x200;
        sendAction.StateAfter2 = 8964;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.TargetActor = 0x1200003;
        sendAction.Type = 4;
        sendAction.GraphState = std::string("graph\0state", 11);

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";
            sendAction.GraphState.clear();

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("AnimationVariables")
    {
        AnimationVariables vars, recvVars;
 
        vars.Booleans.resize(76);
        std::string testString("\xDE\xAD\xBE\xEF"
                          "\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);
        vars.GraphRandom = 0xC34A5C6E;

        Buffer buff(1000);
        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }

        vars.Booleans.resize(33);
        vars.Booleans[16] = false;
        vars.Booleans[17] = false;
        vars.Booleans[18] = false;
        vars.Booleans[19] = false;
        vars.Floats[3] = 42.f;
        vars.Integers[0] = 18;
        vars.Integers[3] = 0;

        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }
    }
}

TEST_CASE("Packets", "[encoding.packets]")
{
    SECTION("RoutedCallRequest and NotifyRoutedCall: every destination")
    {
        Buffer buff(2000);

        RoutedCallRequest toOwner, toHost, toOthers;
        toOwner.To = CallTo::kOwner;
        toOwner.SessionId = 0x21;
        toOwner.ReplyId = 0x4D2;
        toOwner.Call.Class = "Game";
        toOwner.Call.Function = "QueryStat";
        toOwner.Call.Arguments = {std::string("Speechcraft"), 2.5f, int32_t(-3), true, ScriptForm{.SessionId = 0x22},
                                  ScriptForm{.Form = GameId(1, 0x803)}, std::monostate{}};
        toHost.To = CallTo::kHost;
        toHost.Call.Class = "Quest";
        toHost.Call.Function = "SetCurrentStageID";
        toHost.Call.Self = ScriptForm{.Form = GameId(0, 0x1F258)};
        toHost.Call.Arguments = {int32_t(15)};
        toOthers.To = CallTo::kOthers;
        toOthers.Call.Class = "ImageSpaceModifier";
        toOthers.Call.Function = "ApplyCrossFade";
        toOthers.Call.Self = ScriptForm{.Form = GameId(0, 0xF756D)};
        toOthers.Call.Arguments = {2.5f};
        NotifyRoutedCall sendNotify, recvNotify;
        static_cast<Payloads::RoutedCall&>(sendNotify) = toOthers;
        sendNotify.Source = 3;
        sendNotify.Call.Class = "Game";
        sendNotify.Call.Function = "DisablePlayerControls";
        sendNotify.Controls = CallControls::kTakes; // a cutscene's, kept for a joiner

        Buffer::Writer writer(&buff);
        for (const RoutedCallRequest* cpRequest : {&toOwner, &toHost, &toOthers})
            cpRequest->Serialize(writer);
        sendNotify.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        for (const RoutedCallRequest* cpRequest : {&toOwner, &toHost, &toOthers})
        {
            RoutedCallRequest received;
            reader.ReadBits(trash, 8); // pop opcode
            received.DeserializeRaw(reader);
            CHECK(received == *cpRequest);
        }
        reader.ReadBits(trash, 8);
        recvNotify.DeserializeRaw(reader);
        CHECK(recvNotify == sendNotify);
    }

    SECTION("NotifyRespawn distinguishes a replacement from an in-place look change")
    {
        for (const bool cLooks : {false, true})
        {
            Buffer buff(1000);
            NotifyRespawn sent, received;
            sent.ActorId = 7;
            if (cLooks)
            {
                sent.AppearanceBuffer = std::string("face\0race", 9);
                sent.ChangeFlags = 0x3000800;
                sent.FaceTints.Entries.push_back({"WarPaint", 0.75f, 0xABCDEF, 7});
                sent.Weight = 0.f;
            }
            Buffer::Writer writer(&buff);
            sent.Serialize(writer);
            Buffer::Reader reader(&buff);
            uint64_t opcode;
            reader.ReadBits(opcode, 8);
            received.DeserializeRaw(reader);
            REQUIRE(sent == received);
        }
    }

    SECTION("NotifyObjectChanges")
    {
        NotifyObjectChanges sendMessage, recvMessage;
        sendMessage.Cell = GameId(0, 0x133C6);
        ObjectChange contents{ObjectChange::Kind::kContents, GameId(0, 0xA05B5)};
        const std::vector<ObjectChange::ItemCount> cItems = {{GameId(0, 0xF), 120}, {GameId(2, 0x64B3D), 2}};
        contents.Data = ObjectChange::PackContents(cItems);
        REQUIRE(ObjectChange::UnpackContents(contents.Data) == cItems);
        sendMessage.Changes.push_back(contents);
        ObjectChange base{ObjectChange::Kind::kBase, GameId(0, 0xE95D1)};
        base.Item = GameId(0, 0xED5F7);
        sendMessage.Changes.push_back(base);
        ObjectChange create{ObjectChange::Kind::kCreate, GameId(ObjectChange::kCreatedModId, 0x1470)};
        create.Item = GameId(0, 0x319E5);
        ObjectChange::Placement placement{GameId(0, 0x32AE7), {}, {-383.f, 2093.f, 6980.f}, {0.f, 0.f, 1.5f}};
        create.Data = ObjectChange::PackPlacement(placement);
        Inventory::Entry sword;
        sword.BaseId = create.Item;
        sword.Count = 1;
        sword.ExtraEnchantId = GameId(ObjectChange::kCreatedModId, 0x88A);
        sword.ExtraEnchantCharge = 2000;
        sword.EnchantData.IsWeapon = true;
        sword.EnchantData.Effects.push_back({25.f, 0, 1, 12.5f, GameId(0, 0x4605A)});
        sword.ExtraCharge = 1450.f;
        sword.ExtraHealth = 1.2f;
        sword.ExtraPoisonId = GameId(0, 0x73F34);
        sword.ExtraPoisonCount = 3;
        sword.ExtraName = "Dawnfang's Cousin";
        create.Stacks.push_back(sword);
        ObjectChange::Placement unpacked;
        REQUIRE(ObjectChange::UnpackPlacement(create.Data, unpacked));
        CHECK(unpacked.Cell == placement.Cell);
        CHECK(unpacked.Position[1] == 2093.f);
        sendMessage.Changes.push_back(create);
        ObjectChange graph{ObjectChange::Kind::kGraph, GameId(0, 0x847D3)};
        graph.Data = std::string("\x02\x00graph", 7);
        sendMessage.Changes.push_back(graph);
        sendMessage.Changes.push_back({ObjectChange::Kind::kActivate, GameId(0, 0xC149D), GameId(0, 0xA05C4), 3, {}, {}});
        sendMessage.Changes.push_back({ObjectChange::Kind::kAnimation, GameId(0, 0x847D3), {}, 0, {}, "Turn01"});
        sendMessage.Changes.push_back({ObjectChange::Kind::kAnimation, GameId(0, 0x847D6), {}, 0, "Trigger01", "Done"});
        ObjectChange pull{ObjectChange::Kind::kAnimation, GameId(0, 0x1B752), {}, 0, {}, "Pull"};
        pull.Value = 7; // the puller's character
        sendMessage.Changes.push_back(pull);
        sendMessage.Changes.push_back({ObjectChange::Kind::kEnabled, GameId(0, 0xA05C0)});
        ObjectChange enabled{ObjectChange::Kind::kEnabled, GameId(0, 0xA05AC)};
        enabled.Value = 1;
        sendMessage.Changes.push_back(enabled);
        ObjectChange lock{ObjectChange::Kind::kLock, GameId(0, 0x1A2B)};
        lock.Value = 0x132; // locked, level 50
        sendMessage.Changes.push_back(lock);
        ObjectChange harvested{ObjectChange::Kind::kHarvested, GameId(0, 0x10B14E)};
        harvested.Value = 1;
        sendMessage.Changes.push_back(harvested);
        ObjectChange items{ObjectChange::Kind::kItems, GameId(0, 0xA05B5)};
        items.Item = GameId(0, 0x64B3D);
        items.Value = 300;
        Inventory::Entry soulGem;
        soulGem.BaseId = items.Item;
        soulGem.Count = 4;
        soulGem.ExtraSoulLevel = 3;
        items.Stacks.push_back(soulGem);
        sendMessage.Changes.push_back(items);
        ObjectChange saveData{ObjectChange::Kind::kSaveData, GameId(0, 0x32AEC)};
        saveData.Value = 0x11; // form flags, scale
        saveData.Data = std::string("\x00\x08\x00\x00\xC8\x00", 6);
        sendMessage.Changes.push_back(saveData);
        ObjectChange open{ObjectChange::Kind::kOpenState, GameId(0, 0x200CAAC)};
        open.Value = 1;
        sendMessage.Changes.push_back(open);
        ObjectChange move{ObjectChange::Kind::kMove, GameId(0, 0x40248BF)};
        const float cMove[6] = {-736.f, 1664.f, 7031.f, 0.f, 0.f, 4.71f};
        move.Data.assign(reinterpret_cast<const char*>(cMove), sizeof(cMove));
        sendMessage.Changes.push_back(move);
        ObjectChange scale{ObjectChange::Kind::kScale, GameId(0, 0x32AEC)};
        scale.Value = 200;
        sendMessage.Changes.push_back(scale);
        ObjectChange owner{ObjectChange::Kind::kOwner, GameId(0, 0xC2CE5)};
        owner.Item = GameId(0, 0x7);
        sendMessage.Changes.push_back(owner);
        ObjectChange motion{ObjectChange::Kind::kMotion, GameId(0, 0x7998B)};
        motion.Value = 0x104; // keyframed, allowActivate
        sendMessage.Changes.push_back(motion);

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    GIVEN("A walking humanoid's movement snapshot")
    {
        // The human graph's synced variables (Master_Behavior: 59 booleans, 13 floats, 14 integers), as walking sets
        // them: most are zero. Measured on Skyrim before: 112 bytes a snapshot (smooth-movement baseline).
        Movement move;
        move.CellId = GameId(0, 0x31D2E);
        move.Position = glm::vec3(1234.56f, -9876.5f, 512.25f);
        move.Rotation.x = 0.1f;
        move.Rotation.y = 2.5f;
        move.Direction = 1.5707f;
        move.OnGround = true;
        move.Variables.Booleans.assign(59, false);
        move.Variables.Booleans[3] = move.Variables.Booleans[26] = move.Variables.Booleans[34] = true;
        move.Variables.Floats.assign(13, 0.f);
        move.Variables.Floats[0] = -0.03f; // TurnDelta
        move.Variables.Floats[1] = 0.25f;  // Direction
        move.Variables.Floats[2] = 84.2f;  // SpeedSampled
        move.Variables.Floats[4] = 84.f;   // Speed
        move.Variables.Floats[7] = 83.7f;  // SpeedDamped
        move.Variables.Floats[8] = 0.12f;  // Pitch
        move.Variables.Integers.assign(14, 0);
        move.Variables.Integers[0] = move.Variables.Integers[1] = 1;
        move.Variables.GraphRandom = 0x1234;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        move.Serialize(writer);
        Buffer::Reader reader(&buff);
        Movement received;
        received.Deserialize(reader);

        CHECK(received.Variables == move.Variables);
        CHECK(std::abs(received.Direction - move.Direction) < 0.001f);
        CHECK(received.Position == move.Position);
        INFO(writer.Size() << " bytes");
        CHECK(writer.Size() <= 65); // with the bodies layer and GraphsStepPhysics (a byte)
    }
}

TEST_CASE("Joining from the main menu", "[encoding.join]")
{
    const auto roundTrip = [](const auto& acMessage, const auto& acFactory) {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        acMessage.Serialize(writer);
        Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
        auto decoded = acFactory.Extract(reader);
        REQUIRE(decoded);
        using Message = std::remove_cvref_t<decltype(acMessage)>;
        CHECK(*CastUnique<Message>(std::move(decoded)) == acMessage);
        CHECK(reader.GetBitPosition() == writer.GetBitPosition());
    };

    SECTION("A player's character record goes whole")
    {
        PlayerRecordRequest request;
        PlayerRecord& record = request.Record;
        record.Key = "steam:76561198000000001";
        record.Name = "Lydia";
        record.Level = 23;
        record.Base = std::string("\x01\x02\x00\xFF", 4);
        record.BaseFlags = 0x03000E36;
        record.FaceTints.Entries.push_back({"SkinTone", 0.5f, 0xFF8080, 1});
        record.Weight = 100.f;
        record.LevelExperience = 140.5f;
        record.LevelThreshold = 650.f;
        record.Skills[0] = {35.f, 12.25f, 90.f, 0};
        record.Skills[17] = {100.f, 0.f, 0.f, 2};
        record.PerkPoints = 2;
        record.Perks = {{GameId(0, 0xBABE4), 1}, {GameId(0, 0x79342), 3}};
        record.Spells = {GameId(0, 0x12FCD), GameId(0, 0x12FD0)};
        record.Values = {{24, 250.f}, {25, 110.f}, {26, 180.f}, {133, 3.f}};
        record.PermanentValues = {{24, 275.f}, {25, 110.f}, {26, 180.f}, {32, 325.f}};
        record.Words = {GameId(0, 0x13E22), GameId(0, 0x13E23)}; // WordFus, WordRo
        record.Extras["racemenu"] = std::string(70000, 'm'); // past a string's 64 KB
        Inventory::Entry sword;
        sword.BaseId = GameId(0, 0x12EB7);
        sword.Count = 1;
        sword.ExtraWorn = true;
        Inventory::Entry gold;
        gold.BaseId = GameId(0, 0xF);
        gold.Count = 1250;
        const Recipe cFortify{false, {{25.f, 0, 60, 30.f, GameId(0, 0x3EAF3)}, {10.f, 0, 30, 12.f, GameId(0, 0x3EB15)}}};
        Inventory::Entry potions;
        potions.BaseId = cFortify.Id(); // a player-made potion, by its recipe
        potions.Count = 3;
        record.Items.Entries = {sword, gold, potions};
        record.Recipes = {cFortify};
        record.Cell = GameId(0, 0x31D2E);
        record.WorldSpace = GameId(0, 0x3C);
        record.Position = {1024.5f, -2048.25f, 64.f};
        record.Own.Present = true;
        record.Own.FavoriteItems = {{sword, 3}};
        record.Own.Globals[PlayerOwnState::kMagicFavorites] = std::string("\x04\x00\xCD\x7F\x00", 5);
        record.Own.Damage = {-40.f, 0.f, -7.5f};
        record.Own.Effects = {{GameId(0, 0xFB99A), 0, 30.f, 1440.f}};
        record.Own.Cooldowns = {{GameId(0, 0xE40C8), 900.f}};
        record.Own.Trainings = 2;
        roundTrip(request, RequestMessageFactory{});
    }

    SECTION("A record from before recipes has none, and reads whole")
    {
        PlayerRecord record;
        record.Knowledge = {.Present = true};
        Wire::Buffer buffer;
        Wire::Buffer::Writer writer(&buffer);
        record.Serialize(writer);
        PlayerRecord read;
        read.Recipes = {Recipe{true, {{1.f, 0, 1, 1.f, GameId(0, 1)}}}};
        Wire::Buffer::Reader reader(std::span(buffer.GetData(), writer.Size() - 1));
        read.Deserialize(reader);
        CHECK(read == record);
    }

    SECTION("A record an older build saved has no words, and reads whole")
    {
        PlayerRecord record;
        record.Name = "Lydia";
        record.Position = {1.f, 2.f, 3.f};
        Wire::Buffer buffer;
        Wire::Buffer::Writer writer(&buffer);
        record.Serialize(writer);
        // An older build wrote nothing past the place: omit the two trailing empty lists.
        const size_t cOlder = writer.Size() - 2;
        PlayerRecord read;
        read.Words = {GameId(0, 1)};
        Wire::Buffer::Reader reader(std::span(buffer.GetData(), cOlder));
        read.Deserialize(reader);
        CHECK(read == record);
    }

    SECTION("A Dragonborn record with words predates permanent stat totals")
    {
        PlayerRecord record;
        record.Words = {GameId(0, 0x13E22)};
        Wire::Buffer buffer;
        Wire::Buffer::Writer writer(&buffer);
        record.Serialize(writer);
        PlayerRecord read;
        read.PermanentValues = {{24, 999.f}};
        Wire::Buffer::Reader reader(std::span(buffer.GetData(), writer.Size() - 1));
        read.Deserialize(reader);
        CHECK(read == record);
    }

    SECTION("A save chunk carries its place and bytes")
    {
        NotifySaveChunk chunk;
        chunk.Save = std::string(32, 'h');
        chunk.Base = std::string(32, 'b');
        chunk.Size = 8'500'000;
        chunk.Total = 350'000;
        chunk.Offset = 65536;
        chunk.Ahead = true;
        chunk.Data = std::string(NotifySaveChunk::kMaxData, '\x7F');
        roundTrip(chunk, NotifyMessageFactory{});
    }
}

TEST_CASE("Physics bodies", "[encoding.physics]")
{
    BodyFrame frame;
    frame.Key = {.Object = GameId(0, 0x1A0F3), .Index = 2};
    frame.Position = {-182345.53f, 90211.2f, -1024.03f};
    frame.Rotation = glm::normalize(glm::quat(0.3f, -0.6f, 0.2f, 0.7f));
    frame.LinearVelocity = {120.4f, -310.9f, -686.f};
    frame.AngularVelocity = {3.1f, -0.25f, 12.f};
    frame.Gravity = true;

    SECTION("A frame quantizes within a millimetre and a tenth of a degree, and to itself again")
    {
        const BodyFrame cQuantized = frame.Quantized();
        CHECK(glm::distance(cQuantized.Position, frame.Position) < 1.f / 32);
        CHECK(std::abs(glm::dot(cQuantized.Rotation, frame.Rotation)) > std::cos(0.002f));
        CHECK(glm::distance(cQuantized.LinearVelocity, frame.LinearVelocity) < 0.25f);
        CHECK(cQuantized.Quantized() == cQuantized);
    }

    SECTION("A frame takes about BodyFrame::kWireBytes")
    {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        frame.Serialize(writer);
        CHECK(writer.Size() <= BodyFrame::kWireBytes);
    }
}

TEST_CASE("Body states cross both ways, each as its receiver gets it", "[encoding.physics]")
{
    const auto roundTrip = []<class TFactory = NotifyMessageFactory>(const auto& acMessage, TFactory aFactory = {}) {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        acMessage.Serialize(writer);
        Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
        auto decoded = aFactory.Extract(reader);
        REQUIRE(decoded);
        using Message = std::remove_cvref_t<decltype(acMessage)>;
        return *CastUnique<Message>(std::move(decoded));
    };
    const BodyState cMoving{.Frame = {.Key = {.Object = GameId(0, 0x1A0F3), .Index = 2}, .Position = {-182345.53f, 90211.2f, -1024.03f},
                                      .Rotation = glm::normalize(glm::quat(0.3f, -0.6f, 0.2f, 0.7f)), .LinearVelocity = {120.4f, -310.9f, -686.f},
                                      .Gravity = true},
                            .Author = BodyAuthor::kOther, .AuthSeq = 300, .HoldSeq = 2, .Held = true};
    const BodyState cRest{.Frame = {.Key = {.Actor = 0x22, .Index = 14}, .Position = {1.000001f, 2.f, 3.f}, .Knocked = true},
                          .Author = BodyAuthor::kNone, .AuthSeq = 65535, .AtRest = true};
    NotifyBodyStates states;
    states.Tick = 123456789;
    states.States = {cMoving, cRest};
    const auto cDecoded = roundTrip(states);
    REQUIRE(cDecoded.States.size() == 2);
    CHECK(cDecoded.Tick == states.Tick);
    CHECK(cDecoded.States[0] == cMoving.Quantized());
    CHECK(cDecoded.States[1] == cRest.Quantized());
    CHECK(cDecoded.States[1].Frame.Position == cRest.Frame.Position);
    BodyStatesRequest request;
    request.Tick = 7;
    request.States = {cRest};
    CHECK(roundTrip(request, RequestMessageFactory{}).States[0] == cRest.Quantized());
    CHECK(DeliveryOf(request) == Delivery::kUnreliable);
}

TEST_CASE("A player-made enchantment is the same item in every game", "[encoding.items]")
{
    // Each game makes its own form for it (a created 0xFF id): the same effects are the same enchantment.
    Inventory::Entry host;
    host.BaseId = GameId(0, 0x12EB7);
    host.Count = 1;
    host.ExtraEnchantId = GameId(ObjectChange::kCreatedModId, 0x88A);
    host.EnchantData.IsWeapon = true;
    host.EnchantData.Effects.push_back({25.f, 0, 1, 12.5f, GameId(0, 0x4605A)});
    Inventory::Entry guest = host;
    guest.ExtraEnchantId = GameId(ObjectChange::kCreatedModId, 0x913);
    CHECK(host == guest);
    CHECK(host.CanBeMerged(guest));

    guest.EnchantData.Effects[0].Magnitude = 30.f;
    CHECK_FALSE(host.CanBeMerged(guest));
    // A plugin's enchantment is its id.
    host.ExtraEnchantId = GameId(0, 0x49BB7);
    guest = host;
    guest.ExtraEnchantId = GameId(0, 0x45C2A);
    CHECK_FALSE(host.CanBeMerged(guest));
}

TEST_CASE("A spawn carries what its character's announcement held", "[encoding.perks]")
{
    AnnounceCharacterRequest announcement;
    announcement.ReferenceId = GameId(0, 0x14);
    announcement.Weight = 87.5f;
    announcement.EffectIds = {GameId(0, 0x4B2D9), GameId(2, 0x803)};
    announcement.HeldEffects = {{GameId(0, 0xA0370), GameId(0, 0x9F28F)}, {GameId(0, 0xA0371), GameId(0, 0x9F28F), false}};
    announcement.PerksContent.Ranks = {{GameId(0, 0xCC799), 2}, {GameId(3, 0x812), 1}};
    announcement.PerksContent.Spells = {GameId(0, 0x12FD0), GameId(3, 0x813)};
    const auto cSpawn = NotifyCharacterSpawn::FromAnnouncement(announcement, 37, 3);
    CHECK(cSpawn.EffectIds == announcement.EffectIds);
    CHECK(cSpawn.HeldEffects == announcement.HeldEffects);
    CHECK(cSpawn.Weight == 87.5f);
    CHECK(cSpawn.PerksContent == announcement.PerksContent);
}

TEST_CASE("An owner's effect lists are cut at their maximum", "[encoding.traps]")
{
    // Each list holds at most kMaxEffects: a longer one goes out cut, the next field still read in its place.
    RequestActiveEffects many;
    many.SessionId = 9;
    many.EffectIds.assign(257, GameId(0, 0x13CA9));
    many.Held.assign(257, {GameId(0, 0x12FCD), GameId(0, 0x13CA9)});
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    many.Serialize(writer);
    Buffer::Reader reader(&buffer);
    const auto decoded = RequestMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    const auto& cDecoded = static_cast<const RequestActiveEffects&>(*decoded);
    CHECK(cDecoded.EffectIds.size() == Payloads::ActiveEffects::kMaxEffects);
    CHECK(cDecoded.Held.size() == Payloads::ActiveEffects::kMaxEffects);
}

TEST_CASE("A player's own system quests are kept in its record", "[encoding.quests]")
{
    SystemQuestState state;
    SystemQuestState::Quest& vampire = state.Quests.emplace_back();
    vampire.Id = GameId{0, 0xEAFD5};
    vampire.Variables = {{"PlayerVampireQuestScript", "::VampireStatus_var", int32_t{2}},
                         {"PlayerVampireQuestScript", "::LastFeedTime_var", 31.5f},
                         {"PlayerVampireQuestScript", "::CureRace_var", ScriptForm{0, GameId{0, 0x13746}}},
                         {"PlayerVampireQuestScript", "::Unset_var", std::monostate{}}};
    vampire.Timers = {{"RegisterForUpdateGameTime", 12.f}};
    state.Globals = {{GameId{0, 0xEAFD6}, 1.f}};
    const std::string cBlob = state.Encode();
    const auto cDecoded = SystemQuestState::Decode(cBlob);
    REQUIRE(cDecoded);
    CHECK(*cDecoded == state);
    CHECK(cDecoded->Find(GameId{0, 0xEAFD5}) != nullptr);
    CHECK(cDecoded->Find(GameId{0, 0x1}) == nullptr);
    CHECK_FALSE(SystemQuestState::Decode(""));
    CHECK_FALSE(SystemQuestState::Decode(std::string("\x07", 1)));
}

TEST_CASE("An actor's AI state carries what it looks at for each reason, as a part of its record", "[encoding.ai_state][encoding.record]")
{
    ActorAiState state;
    state.Combat = 1;
    state.CombatTarget.SessionId = 2;
    state.HeadTrack[4].SessionId = 1;                  // dialogue: a player's character
    state.HeadTrack[5].RefId = GameId(0, 0x31D2E);     // procedure: a marker from the game's data
    RecordRequest sent;
    sent.Id = 7;
    sent.Part = {RecordKind::kAiState, 3, ToPartBytes(state)};
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = RequestMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == kRecordRequest);
    const auto& cRecord = static_cast<const RecordRequest&>(*decoded);
    CHECK(cRecord == sent);
    ActorAiState read;
    REQUIRE(FromPartBytes(cRecord.Part.Bytes, read));
    CHECK(read == state);
    CHECK(read.HeadTrack[4].SessionId == 1);
    CHECK(read.HeadTrack[5].RefId == GameId(0, 0x31D2E));
    CHECK(read.HeadTrack[0] == AiTarget{});
    CHECK_FALSE(FromPartBytes(std::string{}, read));
}

TEST_CASE("An actor's AI state carries its furniture, combat group and targets, and their absence", "[encoding.ai_state]")
{
    ActorAiState state;
    state.Furniture.RefId = GameId(0, 0x71DFC); // Riverwood's sawmill
    state.Combat = 1;
    state.CombatGroup = (uint64_t{2} << 32) | 7;
    state.GroupTargets = {1, 300};
    ActorAiState read;
    REQUIRE(FromPartBytes(ToPartBytes(state), read));
    CHECK(read == state);
    REQUIRE(FromPartBytes(ToPartBytes(ActorAiState{}), read));
    CHECK(read == ActorAiState{});
}

TEST_CASE("A guest sends movement unreliably and movement with actions or a teleport reliably", "[encoding.delivery]")
{
    ReferencesMoveRequest movement;
    movement.Updates[1].UpdatedMovement.Position = glm::vec3(1.f, 2.f, 3.f);
    CHECK(DeliveryOf(movement) == Delivery::kUnreliable);
    movement.Updates[2].UpdatedMovement.Teleported = true;
    CHECK(DeliveryOf(movement) == Delivery::kReliable);
    movement.Updates[2].UpdatedMovement.Teleported = false;
    movement.Updates[2].ActionEvents.emplace_back();
    CHECK(DeliveryOf(movement) == Delivery::kReliable);
    CHECK(DeliveryOf(BodyStatesRequest{}) == Delivery::kUnreliable);
    CHECK(DeliveryOf(PartyTravelRequest{}) == Delivery::kReliableNow);
}

namespace
{
GraphOps GraphOpsBatch(uint64_t aFirstTick, uint32_t aActor, uint32_t aFirstUpdate, std::vector<GraphOp> aOps, size_t aFrames)
{
    GraphOps ops;
    for (size_t i = 0; i < aFrames; ++i)
        ops.Frames.push_back({aFirstTick + i, 0.0166f + 0.0001f * static_cast<float>(i)});
    ops.Runs.push_back({aActor, aFirstUpdate, std::move(aOps)});
    return ops;
}
} // namespace

TEST_CASE("Graph ops round trip exactly", "[encoding.graph_ops]")
{
    const float cOdd = std::bit_cast<float>(0x7FC00001u); // a NaN's payload survives: values go as bits
    GraphOps ops = GraphOpsBatch(1000, 7, 0,
                                 {{.Frame = 0, .Seed = GraphSeed{11, 12}, .Changed = {{0, 0x42A343EB}, {40, std::bit_cast<uint32_t>(cOdd)}}},
                                  {.Event = true, .EventId = 513, .Changed = {{48, 1}}},
                                  {.Event = true, .EventId = 77, .ClipSet = 4}, // a clip load: an instant interaction's set
                                  {.Event = true, .EventId = 78, .ClipSet = 47}, // a movement idle's
                                  {.Frame = 1},
                                  {.Generate = true}, // its generate pass, with the update
                                  {.Frame = 2, .Changed = {{0, 0x42A343EC}}, .Picks = {{31, -1}, {40, 3}}}, // its nodes' picks
                                  {.Generate = true},
                                  {.Frame = 3, .Picks = {{31, 2}}}, // a pick alone is no empty update
                                  {.Generate = true},
                                  {.Frame = 4},
                                  {.Event = true, .EventId = 6},
                                  {.Generate = true}, // after something else: an op of its own
                                  {.Frame = 5}}, // a skipped frame
                                 6);
    ops.Runs.push_back({9, 40, {{.Frame = 0, .Changed = {{0, 0x42A343EC}, {700, 9}}}, {.Frame = 1}}});
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    ops.Serialize(writer);
    Wire::Buffer::Reader reader(&buffer);
    GraphOps read;
    REQUIRE(read.Deserialize(reader));
    CHECK(read == ops);
}

TEST_CASE("An update that changes nothing costs a bit", "[encoding.graph_ops]")
{
    // With its generate pass after it, as every frame a graph updates in has.
    std::vector<GraphOp> idle{{.Frame = 0}, {.Generate = true}};
    for (uint32_t i = 1; i < 60; ++i)
    {
        idle.push_back({.Frame = i});
        idle.push_back({.Generate = true});
    }
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    GraphOps::WriteRun(writer, {7, 0, idle});
    CHECK(writer.Size() <= 3 + 3 + 8); // id, first update and count; the first op; then 59 bits
    Wire::Buffer::Reader reader(&buffer);
    GraphOpsRun read;
    REQUIRE(GraphOps::ReadRun(reader, read, 60));
    CHECK(read.Ops == idle);
}

TEST_CASE("Graph ops reject truncated and impossible input", "[encoding.graph_ops]")
{
    const GraphOps cOps = GraphOpsBatch(50, 3, 0, {{.Frame = 0, .Changed = {{2, 5}, {9, 0xFFFFFFFF}}}, {.Event = true, .EventId = 4}, {.Frame = 1}}, 2);
    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    cOps.Serialize(writer);
    const size_t cBytes = writer.Size();
    for (size_t size = 0; size + 1 < cBytes; ++size)
    {
        Wire::Buffer::Reader reader(std::span<const uint8_t>(buffer.GetData(), size));
        GraphOps read;
        CHECK_FALSE(read.Deserialize(reader));
    }
    // An update in a frame the batch does not have.
    Wire::Buffer bad;
    Wire::Buffer::Writer badWriter(&bad);
    GraphOpsBatch(50, 3, 0, {{.Frame = 3}}, 2).Serialize(badWriter);
    Wire::Buffer::Reader badReader(&bad);
    GraphOps read;
    CHECK_FALSE(read.Deserialize(badReader));
}

TEST_CASE("The host's eye of a guest round-trips, unseen and seen", "[encoding][stealth]")
{
    for (const auto& [cLevel, cSeeing, cStealth] : {std::tuple{NotifyStealthEye::kUnseen, 0u, 100.f}, std::tuple{146, 2u, 0.f}, std::tuple{-7, 1u, 79.5f}})
    {
        NotifyStealthEye eye;
        eye.Level = cLevel;
        eye.Seeing = cSeeing;
        eye.Stealth = cStealth;
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        eye.Serialize(writer);
        Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
        auto message = NotifyMessageFactory{}.Extract(reader);
        REQUIRE(message);
        CHECK(*CastUnique<NotifyStealthEye>(std::move(message)) == eye);
    }
}

TEST_CASE("A passenger's ride part names its horse", "[encoding][record-parts]")
{
    RidePart read{7, 7};
    REQUIRE(FromPartBytes(ToPartBytes(RidePart{900000000123, 0x1234}), read));
    CHECK(read == RidePart{900000000123, 0x1234});
    REQUIRE(FromPartBytes(ToPartBytes(RidePart{}), read));
    CHECK(read == RidePart{});
}

TEST_CASE("A cast part carries each source's held cast, and a cleared one", "[encoding][record-parts]")
{
    CastPart sent;
    sent.Sources[1] = HeldCast{GameId(0, 0x12FCD), true, 0x2A}; // Flames in the right hand, dual, at an actor
    sent.Sources[2] = HeldCast{GameId(1, 0x803)};
    CastPart read;
    read.Sources[0] = HeldCast{GameId(0, 1)}; // a slot not held any more reads cleared
    REQUIRE(FromPartBytes(ToPartBytes(sent), read));
    CHECK(read == sent);
    REQUIRE(FromPartBytes(ToPartBytes(CastPart{}), read));
    CHECK(read == CastPart{});
}

TEST_CASE("A ragdoll part carries every body's rest and its time", "[encoding][record-parts]")
{
    RagdollPart sent;
    sent.Tick = 900000000456;
    sent.Rests = {{.Key = {.Actor = 0x2A, .Index = 0}, .Position = {1.5f, -2.f, 300.f}, .Rotation = {0.f, 0.f, 0.f, 1.f}},
                  {.Key = {.Actor = 0x2A, .Index = 17}, .Position = {4.f, 5.f, 6.f}, .Knocked = true}};
    RagdollPart read;
    REQUIRE(FromPartBytes(ToPartBytes(sent), read));
    CHECK(read.Tick == sent.Tick);
    REQUIRE(read.Rests.size() == 2);
    CHECK(read.Rests[1].Key.Index == 17);
    CHECK(read.Rests[1].Knocked);
    CHECK(glm::distance(read.Rests[0].Position, sent.Rests[0].Position) < 0.1f);
}

TEST_CASE("An actor state part round-trips with its noise", "[encoding][record-parts]")
{
    ActorStatePart part;
    part.Tick = 900000000123;
    part.Flags1 = 1u << 9;
    part.Flags2 = 0x100;
    part.InKillMove = true;
    part.Killer = 3;
    part.HandsUp = true;
    part.Noise = 100;
    ActorStatePart read;
    REQUIRE(FromPartBytes(ToPartBytes(part), read));
    CHECK(read == part);
    CHECK_FALSE(read.SameState(ActorStatePart{part.Tick, part.Flags1, part.Flags2, true, 3, true, 0})); // noise alone is news
}

TEST_CASE("A record part longer than 64 KB round-trips whole", "[encoding][record-parts]")
{
    // A full items part of a large inventory (review of PR #83): WriteString's 16-bit length would cut it.
    RecordPart part{RecordKind::kItems, 7, std::string(70000, '\x5A')};
    part.Bytes[69999] = '\x01';
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    part.Serialize(writer);
    Buffer::Reader reader(&buffer);
    RecordPart read;
    read.Deserialize(reader);
    CHECK(read.Kind == RecordKind::kItems);
    CHECK(read.Version == 7);
    CHECK(read.Bytes == part.Bytes);
}

namespace
{
// [opcode][varints]: a message as any sender could craft it.
std::vector<uint8_t> Crafted(uint8_t aOpcode, std::initializer_list<uint64_t> aVarInts)
{
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    writer.WriteBits(aOpcode, 8);
    for (const uint64_t cValue : aVarInts)
        Serialization::WriteVarInt(writer, cValue);
    return {buffer.GetData(), buffer.GetData() + writer.Size()};
}

template <class TMessage> std::vector<uint8_t> Written(const TMessage& acMessage)
{
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    acMessage.Serialize(writer);
    return {buffer.GetData(), buffer.GetData() + writer.Size()};
}

// The whole of acBytes decodes; every cut short of its end decodes to nothing.
template <class TFactory> void CheckEveryCutFails(const std::vector<uint8_t>& acBytes, const TFactory& acFactory)
{
    Buffer::Reader whole{std::span(acBytes)};
    REQUIRE(acFactory.Extract(whole));
    for (size_t size = 0; size < acBytes.size(); ++size)
    {
        Buffer::Reader reader{std::span(acBytes.data(), size)};
        INFO("cut at " << size << " of " << acBytes.size());
        CHECK_FALSE(acFactory.Extract(reader));
    }
}
} // namespace

TEST_CASE("A count is bounded by its maximum and by the bits left", "[encoding.decoder]")
{
    SECTION("within both, it is read")
    {
        const auto cBytes = Crafted(0, {3, 0, 0, 0});
        Buffer::Reader reader{std::span(cBytes)};
        uint64_t opcode = 0;
        REQUIRE(reader.ReadBits(opcode, 8));
        CHECK(Serialization::ReadCount(reader, 3) == 3);
        CHECK_FALSE(reader.Failed());
    }
    SECTION("past its maximum, the reader fails")
    {
        const auto cBytes = Crafted(0, {4, 0, 0, 0, 0});
        Buffer::Reader reader{std::span(cBytes)};
        uint64_t opcode = 0;
        REQUIRE(reader.ReadBits(opcode, 8));
        CHECK(Serialization::ReadCount(reader, 3) == 0);
        CHECK(reader.Failed());
    }
    SECTION("past the bits left, the reader fails")
    {
        const auto cBytes = Crafted(0, {9});
        Buffer::Reader reader{std::span(cBytes)};
        uint64_t opcode = 0;
        REQUIRE(reader.ReadBits(opcode, 8));
        CHECK(Serialization::ReadCount(reader, 1000) == 0);
        CHECK(reader.Failed());
    }
    SECTION("a short read stays failed")
    {
        const uint8_t cByte = 0;
        Buffer::Reader reader{std::span(&cByte, 1)};
        uint64_t value = 0;
        CHECK_FALSE(reader.ReadBits(value, 16));
        CHECK(reader.ReadBits(value, 8));
        CHECK(reader.Failed());
    }
}

TEST_CASE("A crafted count of 2^40 is dropped at once", "[encoding.decoder]")
{
    const uint64_t cHuge = uint64_t{1} << 40;
    SECTION("references moved, both ways")
    {
        const auto cRequest = Crafted(kReferencesMoveRequest, {7, cHuge});
        Buffer::Reader request{std::span(cRequest)};
        CHECK_FALSE(RequestMessageFactory{}.Extract(request));
        const auto cNotify = Crafted(kNotifyReferencesMove, {7, cHuge});
        Buffer::Reader notify{std::span(cNotify)};
        CHECK_FALSE(NotifyMessageFactory{}.Extract(notify));
    }
    SECTION("other messages sized by a count")
    {
        const auto cItems = Crafted(kNotifyPartyItems, {cHuge});
        Buffer::Reader items{std::span(cItems)};
        CHECK_FALSE(NotifyMessageFactory{}.Extract(items));
        // A cell (two varints), then the count.
        const auto cCell = Crafted(kCellStateRequest, {0, 1, cHuge});
        Buffer::Reader cell{std::span(cCell)};
        CHECK_FALSE(RequestMessageFactory{}.Extract(cell));
        // A container, an item (two varints each) and a count, then the stacks.
        const auto cContainer = Crafted(kContainerChangeRequest, {0, 1, 0, 2, 1, cHuge});
        Buffer::Reader container{std::span(cContainer)};
        CHECK_FALSE(RequestMessageFactory{}.Extract(container));
    }
    SECTION("a count within the message's maximum but past its bytes")
    {
        const auto cItems = Crafted(kNotifyPartyItems, {100});
        Buffer::Reader items{std::span(cItems)};
        CHECK_FALSE(NotifyMessageFactory{}.Extract(items));
    }
}

TEST_CASE("A truncated message decodes to nothing, not zeros", "[encoding.decoder]")
{
    ReferencesMoveRequest move;
    move.Tick = 12345;
    auto& update = move.Updates[42];
    update.UpdatedMovement.Position = {1.f, 2.f, 3.f};
    update.ActionEvents.emplace_back().EventName = "attackStart";
    CheckEveryCutFails(Written(move), RequestMessageFactory{});

    NotifyPartyItems items;
    items.Items.resize(2);
    items.Items[0].BaseId = GameId(0, 0x12EB7);
    items.Items[0].ExtraName = "[P] Iron Sword";
    items.Items[1].BaseId = GameId(0, 0x93B08);
    CheckEveryCutFails(Written(items), NotifyMessageFactory{});

    AuthenticationRequest join;
    join.Username = "Lydia";
    join.HeldSaves = {std::string(32, 'a')};
    CheckEveryCutFails(Written(join), RequestMessageFactory{});
}
