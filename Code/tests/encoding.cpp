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

#include <Messages/RequestMessageFactory.h>
#include <Messages/NotifyMessageFactory.h>
#include <Messages/NotifyTimeSettings.h>
#include <Messages/WorldState.h>
#include <Structs/Vector2_NetQuantize.h>

#include <numbers>

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

TEST_CASE("Encoding factory", "[encoding.factory]")
{
    Buffer buff(1000);

    {
        AuthenticationRequest request;
        request.Token = "TesSt";

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const RequestMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<AuthenticationRequest>(std::move(pMessage));
        REQUIRE(pRequest->Token == request.Token);
    }

    {
        PlayerLevelRequest request;
        request.NewLevel = 42;

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const RequestMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<PlayerLevelRequest>(std::move(pMessage));
        REQUIRE(pRequest->NewLevel == request.NewLevel);
    }
}

TEST_CASE("Static structures", "[encoding.static]")
{
    GIVEN("GameId")
    {
        GameId sendObjects, recvObjects;
        sendObjects.ModId = 1456987;
        sendObjects.BaseId = 0x789654;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector3_NetQuantize")
    {
        Vector3_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 142.56f;
        sendObjects.y = 45687.7f;
        sendObjects.z = -142.56f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector2_NetQuantize")
    {
        Vector2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1000.89f;
        sendObjects.y = -485632.75f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize")
    {
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1.89f;
        sendObjects.y = std::numbers::pi_v<float> * 2.0f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize needing wrap")
    {
        // This test is a bit dangerous as floating errors can lead to sendObjects != recvObjects but the difference is minuscule so we don't care abut such cases
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = -1.87f;
        sendObjects.y = std::numbers::pi_v<float> * 18.0f + 3.6f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
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
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.TargetActor = 0x1200003;
        sendAction.Type = 4;

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

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("Full Mods")
    {
        Mods sendMods, recvMods;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);

        sendMods.ModList.push_back({"Hello", 42});
        sendMods.ModList.push_back({"Hi", 14});
        sendMods.ModList.push_back({"Test", 8});
        sendMods.ModList.push_back({"Toast", 49});

        sendMods.Serialize(writer);

        Buffer::Reader reader(&buff);
        recvMods.Deserialize(reader);

        REQUIRE(sendMods == recvMods);
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
    SECTION("AuthenticationRequest")
    {
        Buffer buff(1000);

        AuthenticationRequest sendMessage, recvMessage;
        sendMessage.Token = "TesSt";
        sendMessage.Game = {GameVersions::Pack(1, 6, 1170), GameVersions::Pack(2, 2, 6)};
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AuthenticationResponse")
    {
        Buffer buff(1000);

        AuthenticationResponse sendMessage, recvMessage;
        sendMessage.Type = AuthenticationResponse::ResponseType::kWrongGame;
        sendMessage.Version = "v1.6.7-0-gdeadbeef";
        sendMessage.Game = {GameVersions::Pack(1, 6, 640), GameVersions::Pack(2, 2, 3)};
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("CancelAssignmentRequest")
    {
        Buffer buff(1000);

        CancelAssignmentRequest sendMessage, recvMessage;
        sendMessage.Cookie = 14523698;
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("CellStateRequest")
    {
        Buffer buff(1000);

        CellStateRequest sendMessage, recvMessage;
        sendMessage.Cell = GameId(0, 0x133C6);
        sendMessage.Changed = {{GameId(0, 0x1A6F6), 0x1000}, {GameId(2, 0x803), 0x800001}};
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("ContainerChangeRequest")
    {
        Buffer buff(1000);

        ContainerChangeRequest sendMessage, recvMessage;
        sendMessage.Container = GameId(0, 0xA05B5);
        sendMessage.Item = GameId(2, 0x64B3D);
        sendMessage.Count = 70000;
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("RequestRespawn")
    {
        for (const bool cHasState : {false, true})
        {
            Buffer buff(1000);

            RequestRespawn sendMessage, recvMessage;
            sendMessage.ActorId = 7;
            sendMessage.AppearanceBuffer = "werewolf";
            sendMessage.ChangeFlags = 0x2000840;
            sendMessage.HasState = cHasState;
            if (cHasState)
            {
                sendMessage.LatestAction.ActionId = 0x13002;
                sendMessage.LatestAction.EventName = "IdleWerewolfTransformation";
                sendMessage.GraphState = "graph";
                sendMessage.Data.InitialActorValues.ActorMaxValuesList[24] = 5000.f;
                sendMessage.Data.IsWeaponDrawn = true;
            }
            Buffer::Writer writer(&buff);
            sendMessage.Serialize(writer);

            Buffer::Reader reader(&buff);

            uint64_t trash;
            reader.ReadBits(trash, 8); // pop opcode

            recvMessage.DeserializeRaw(reader);

            REQUIRE(sendMessage == recvMessage);
        }
    }

    SECTION("AssignCharacterRequest")
    {
        Buffer buff(1000);

        ActionEvent sendAction;
        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.TargetActor = 0x1200003;
        sendAction.Type = 4;

        AssignCharacterRequest sendMessage, recvMessage;
        sendMessage.Cookie = 14523698;
        sendMessage.AppearanceBuffer = "toto";
        sendMessage.CellId.BaseId = 45;
        sendMessage.FormId.ModId = 48;
        sendMessage.ReferenceId.BaseId = 456799;
        sendMessage.ReferenceId.ModId = 4079;
        sendMessage.LatestAction = sendAction;
        sendMessage.Position.x = -452.4f;
        sendMessage.Position.y = 452.4f;
        sendMessage.Position.z = 125452.4f;
        sendMessage.Rotation.x = -1.87f;
        sendMessage.Rotation.y = 45.35f;

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyTimeSettings")
    {
        NotifyTimeSettings sendMessage, recvMessage;
        sendMessage.timeModel.TimeScale = 6.f;
        sendMessage.timeModel.Time = 23.75f;
        sendMessage.timeModel.Day = 31;
        sendMessage.timeModel.Month = 11;
        sendMessage.timeModel.Year = 201;
        sendMessage.timeModel.DaysPassed = 300;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyGlobals")
    {
        NotifyGlobals sendMessage, recvMessage;
        sendMessage.Values.push_back({GameId(0, 0x87E1C), 1.f});
        sendMessage.Values.push_back({GameId(3, 0x801), -0.5f});

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
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
        sendMessage.Changes.push_back({ObjectChange::Kind::kEnabled, GameId(0, 0xA05C0)});
        ObjectChange enabled{ObjectChange::Kind::kEnabled, GameId(0, 0xA05AC)};
        enabled.Value = 1;
        sendMessage.Changes.push_back(enabled);
        ObjectChange lock{ObjectChange::Kind::kLock, GameId(0, 0x1A2B)};
        lock.Value = 0x132; // locked, level 50
        sendMessage.Changes.push_back(lock);
        ObjectChange items{ObjectChange::Kind::kItems, GameId(0, 0xA05B5)};
        items.Item = GameId(0, 0x64B3D);
        items.Value = 300;
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

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("NotifyQuests")
    {
        NotifyQuests sendMessage, recvMessage;
        sendMessage.Join = true;
        auto& quest = sendMessage.Quests.emplace_back();
        quest.Id = GameId(0, 0x36191);
        quest.Flags = 0x1;
        quest.CurrentStage = 150;
        quest.DoneStages = {10, 20, 140, 150};
        quest.Objectives = {{10, 3}, {20, 1}};
        quest.Aliases = {{4, GameId(0, 0x1A677), false}, {9, GameId(), true}, {11, GameId(), false},
                         {5, GameId(0, 0x10A7CC), false, GameId(0, 0xB6BE6)}, {6, GameId(0, 0x1D4F0), false, GameId(0, 0x3C), -12, 7}};
        quest.Locations = {{2, GameId(0, 0x18A56)}, {3, GameId()}};
        sendMessage.Quests.emplace_back().Id = GameId(2, 0x801);

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    GIVEN("ReferencesMoveRequest")
    {
        ReferencesMoveRequest sendMessage, recvMessage;
        auto& update = sendMessage.Updates[1];
        auto& move = update.UpdatedMovement;

        AnimationVariables vars;
        vars.Booleans.resize(76);
        std::string testString("\xDE\xAD\xBE\xEF\x76\xB");
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

        move.Variables = vars;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.Updates[1].UpdatedMovement == sendMessage.Updates[1].UpdatedMovement);
    }
}
