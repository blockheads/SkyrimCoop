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
#include <Messages/DroppedItems.h>
#include <Structs/Vector2_NetQuantize.h>
#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>

#include <numbers>

TEST_CASE("NPC activations carry the target's session id", "[encoding.followers]")
{
    ActivateRequest request;
    request.Id = {};
    request.CellId = GameId(0, 0x32AE7);
    request.NpcId = 1234;
    request.ActivatorId = 42;
    request.PreActivationOpenState = 0;
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    request.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = RequestMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    CHECK(static_cast<const ActivateRequest&>(*decoded) == request);
    NotifyActivate notice;
    notice.NpcId = request.NpcId;
    notice.ActivatorId = request.ActivatorId;
    notice.PreActivationOpenState = 0;
    Buffer result;
    Buffer::Writer resultWriter(&result);
    notice.Serialize(resultWriter);
    Buffer::Reader resultReader(&result);
    auto received = NotifyMessageFactory{}.Extract(resultReader);
    REQUIRE(received);
    CHECK(static_cast<const NotifyActivate&>(*received) == notice);
}

TEST_CASE("Crime messages retain actions and absolute faction buckets", "[encoding.crime]")
{
    CrimeRequest request;
    request.Sequence = 1234;
    request.Action = CrimeAction::ContainerTheft;
    request.Target = GameId(0xFE, 0x100812);
    request.TargetActor = 987;
    request.Item = GameId(2, 0xABC);
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

    NotifyCrimeState state;
    state.Gold = {{GameId(0, 0x28170), 1000, 25, 75, 3000, true}, {GameId(0xFE, 0x100812), 0, 50, 80, 0},
                  {GameId(0, 0x2816B), 0, 0, 0, 0, true}};
    Buffer reply;
    Buffer::Writer replyWriter(&reply);
    state.Serialize(replyWriter);
    Buffer::Reader replyReader(&reply);
    auto notification = NotifyMessageFactory{}.Extract(replyReader);
    REQUIRE(notification);
    REQUIRE(notification->GetOpcode() == NotifyCrimeState::Opcode);
    CHECK(static_cast<const NotifyCrimeState&>(*notification).Gold == state.Gold);

    NotifyLockCrime caught;
    caught.Attempt = 81;
    Buffer lock;
    Buffer::Writer lockWriter(&lock);
    caught.Serialize(lockWriter);
    Buffer::Reader lockReader(&lock);
    auto decision = NotifyMessageFactory{}.Extract(lockReader);
    REQUIRE(decision);
    REQUIRE(decision->GetOpcode() == NotifyLockCrime::Opcode);
    CHECK(static_cast<const NotifyLockCrime&>(*decision).Attempt == 81);

    NotifyCrimeConfiscation confiscation;
    confiscation.Id = 19;
    confiscation.Faction = GameId(0, 0x28170);
    Buffer fine;
    Buffer::Writer fineWriter(&fine);
    confiscation.Serialize(fineWriter);
    Buffer::Reader fineReader(&fine);
    auto due = NotifyMessageFactory{}.Extract(fineReader);
    REQUIRE(due);
    REQUIRE(due->GetOpcode() == NotifyCrimeConfiscation::Opcode);
    CHECK(static_cast<const NotifyCrimeConfiscation&>(*due).Id == confiscation.Id);
    CHECK(static_cast<const NotifyCrimeConfiscation&>(*due).Faction == confiscation.Faction);
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
        CHECK_FALSE(decoded.IsValid());
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

TEST_CASE("Encoding factory", "[encoding.factory]")
{
    Buffer buff(1000);

    {
        AuthenticationRequest request;
        request.Username = "TesSt";

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const RequestMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<AuthenticationRequest>(std::move(pMessage));
        REQUIRE(pRequest->Username == request.Username);
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

TEST_CASE("Session packets carry only difficulty and chat content", "[encoding.session]")
{
    const auto check = [](const auto& acMessage, const auto& acFactory, const std::vector<uint8_t>& acExpected)
    {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        acMessage.Serialize(writer);
        CHECK(std::vector<uint8_t>(buffer.GetData(), buffer.GetData() + writer.Size()) == acExpected);

        Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
        auto decoded = acFactory.Extract(reader);
        REQUIRE(decoded);
        using Message = std::remove_cvref_t<decltype(acMessage)>;
        CHECK(*CastUnique<Message>(std::move(decoded)) == acMessage);
        CHECK(reader.GetBitPosition() == writer.GetBitPosition());
    };

    SECTION("Authentication has no token between game and build")
    {
        AuthenticationRequest request;
        request.Game = {1, 2};
        request.Version = "v";
        request.Username = "L";
        request.Level = 7;
        request.PlayerTime = {0.f, 0.f, 0, 0, 0, 0};
        std::vector<uint8_t> expected{uint8_t(kAuthenticationRequest), 1, 2, 1, 'v', 0, 0x20, 0, 'L', 0, 0, 0, 0, 7};
        expected.resize(28, 0); // ... an empty player key, not from the main menu
        check(request, RequestMessageFactory{}, expected);
    }

    SECTION("Authentication returns difficulty without option flags")
    {
        AuthenticationResponse response;
        response.Type = AuthenticationResponse::ResponseType::kAccepted;
        response.Version = "v";
        response.Game = {1, 2};
        response.Difficulty = 5;
        response.PlayerId = 7;
        check(response, NotifyMessageFactory{}, {uint8_t(kAuthenticationResponse), 0, 1, 'v', 1, 2, 0, 0xA0, 0xE0, 0, 0});
    }

    SECTION("Difficulty changes carry one value")
    {
        NotifySettingsChange message;
        message.Difficulty = 5;
        check(message, NotifyMessageFactory{}, {uint8_t(kNotifySettingsChange), 5});
    }

    SECTION("Players send only text")
    {
        SendChatMessageRequest request;
        request.ChatMessage = "Hi";
        check(request, RequestMessageFactory{}, {uint8_t(kSendChatMessageRequest), 2, 'H', 'i'});
    }

    SECTION("Player and system broadcasts remain distinct")
    {
        NotifyChatMessageBroadcast line;
        line.PlayerName = "L";
        line.ChatMessage = "Hi";
        check(line, NotifyMessageFactory{}, {uint8_t(kNotifyChatMessageBroadcast), 1, 1, 'L', 2, 'H', 'i'});
        line.MessageType = kSystemMessage;
        line.PlayerName.clear();
        check(line, NotifyMessageFactory{}, {uint8_t(kNotifyChatMessageBroadcast), 0, 0, 2, 'H', 'i'});
    }

    SECTION("Active refusal codes keep their wire values")
    {
        CHECK(uint8_t(AuthenticationResponse::ResponseType::kSessionFull) == 5);
        CHECK(uint8_t(AuthenticationResponse::ResponseType::kWrongGame) == 6);
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
        sendMessage.Username = "Lydia";
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
        sendMessage.PlayerId = 3;
        sendMessage.CharacterId = 41;
        sendMessage.Version = "v1.6.7-0-gdeadbeef";
        sendMessage.Difficulty = 5;
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
        Inventory::Entry tempered;
        tempered.BaseId = sendMessage.Item;
        tempered.Count = 2;
        tempered.ExtraHealth = 1.6f;
        sendMessage.Stacks.push_back(tempered);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("DialogueOptionsRequest, NotifyDialogueList and NotifyDialogueOpen")
    {
        Buffer buff(1000);

        DialogueOption option;
        option.Topic = GameId(0, 0x1F4E2);
        option.Info = GameId(2, 0x803);
        option.Quest = GameId(0, 0x46F20);
        option.Text = "(Persuade) Surely you can help.";
        DialogueOptionsRequest sendRequest, recvRequest;
        sendRequest.Conversation = 3;
        sendRequest.Step = 7;
        sendRequest.Choices = true;
        sendRequest.Options = {option, option};
        NotifyDialogueList sendList, recvList;
        sendList.Conversation = 3;
        sendList.Step = 8;
        sendList.Options = {option};
        NotifyDialogueOpen sendOpen, recvOpen;
        sendOpen.Conversation = 4;
        sendOpen.NpcId = 0x21;
        sendOpen.Everyone = true;
        sendOpen.Greeting = GameId(2, 0x803);

        Buffer::Writer writer(&buff);
        sendRequest.Serialize(writer);
        sendList.Serialize(writer);
        sendOpen.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvRequest.DeserializeRaw(reader);
        reader.ReadBits(trash, 8);
        recvList.DeserializeRaw(reader);
        reader.ReadBits(trash, 8);
        recvOpen.DeserializeRaw(reader);

        CHECK((recvRequest.Conversation == 3 && recvRequest.Step == 7 && recvRequest.Choices && recvRequest.Options == sendRequest.Options));
        CHECK((recvList.Step == 8 && !recvList.Choices && recvList.Options == sendList.Options));
        CHECK((recvOpen.Conversation == 4 && recvOpen.NpcId == 0x21 && recvOpen.Everyone));
        CHECK(recvOpen.Greeting == sendOpen.Greeting);
        DialogueOpenedRequest sendOpened, recvOpened;
        sendOpened.NpcId = 0x21;
        sendOpened.Everyone = true;
        sendOpened.Greeting = GameId(0xFE, 0x123ABC);
        Buffer greetingBuffer(100);
        Buffer::Writer greetingWriter(&greetingBuffer);
        sendOpened.Serialize(greetingWriter);
        Buffer::Reader greetingReader(&greetingBuffer);
        greetingReader.ReadBits(trash, 8);
        recvOpened.DeserializeRaw(greetingReader);
        CHECK(recvOpened.Greeting == sendOpened.Greeting);
    }

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

    SECTION("CallEndedRequest and NotifyCallEnded")
    {
        Buffer buff(1000);

        CallEndedRequest sendRequest, recvRequest;
        sendRequest.Key = 0x5A;
        sendRequest.Result = int32_t(2);
        NotifyCallEnded sendNotify, recvNotify;
        static_cast<Payloads::CallEnded&>(sendNotify) = sendRequest;
        sendNotify.Source = 3;

        Buffer::Writer writer(&buff);
        sendRequest.Serialize(writer);
        sendNotify.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvRequest.DeserializeRaw(reader);
        reader.ReadBits(trash, 8);
        recvNotify.DeserializeRaw(reader);

        CHECK(recvRequest == sendRequest);
        CHECK(recvNotify == sendNotify);
    }

    SECTION("PlayerNativeResultRequest and NotifyPlayerNativeResult")
    {
        Buffer buff(1000);

        PlayerNativeResultRequest sendRequest, recvRequest;
        sendRequest.ReplyId = 0x4D2;
        sendRequest.Result = int32_t(7);
        NotifyPlayerNativeResult sendNotify, recvNotify;
        sendNotify.ReplyId = 0x4D3;
        sendNotify.Result = ScriptForm{.SessionId = 0x22};

        Buffer::Writer writer(&buff);
        sendRequest.Serialize(writer);
        sendNotify.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvRequest.DeserializeRaw(reader);
        reader.ReadBits(trash, 8);
        recvNotify.DeserializeRaw(reader);

        CHECK(recvRequest == sendRequest);
        CHECK(recvNotify == sendNotify);
    }

    SECTION("RequestRemoveCharacter and NotifyRemoveCharacter")
    {
        Buffer buff(1000);

        RequestRemoveCharacter sendRequest, recvRequest;
        sendRequest.SessionId = 42;
        sendRequest.Whereabouts.Where = ActorWhereabouts::State::kPlaced;
        sendRequest.Whereabouts.CellId = GameId(0, 0x3C);
        sendRequest.Whereabouts.WorldSpaceId = GameId(0, 0x3C);
        sendRequest.Whereabouts.Position = {1000.f, -2000.f, 300.f};
        sendRequest.Whereabouts.Heading = 2.25f;
        NotifyRemoveCharacter sendNotify, recvNotify;
        sendNotify.SessionId = 43;
        sendNotify.Whereabouts.Where = ActorWhereabouts::State::kDeleted;

        Buffer::Writer writer(&buff);
        sendRequest.Serialize(writer);
        sendNotify.Serialize(writer);

        Buffer::Reader reader(&buff);
        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode
        recvRequest.DeserializeRaw(reader);
        reader.ReadBits(trash, 8);
        recvNotify.DeserializeRaw(reader);

        REQUIRE(sendRequest == recvRequest);
        REQUIRE(sendNotify == recvNotify);
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
            sendMessage.FaceTints.Entries.push_back({"SkinTone", 0.5f, 0x8000FF, 6});
            sendMessage.Weight = 12.5f;
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

    SECTION("AnnounceCharacterRequest")
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

        AnnounceCharacterRequest sendMessage, recvMessage;
        sendMessage.SessionId = 14523698;
        sendMessage.AppearanceBuffer = "toto";
        sendMessage.Weight = 87.5f;
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
        sendMessage.EffectIds = {GameId(0, 0x4B2D9), GameId(2, 0x803)};

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);

        // The spawn others get carries the same.
        NotifyCharacterSpawn sendSpawn = NotifyCharacterSpawn::FromAnnouncement(sendMessage, 7, 0), recvSpawn;
        CHECK(sendSpawn.EffectIds == sendMessage.EffectIds);
        CHECK(sendSpawn.Weight == 87.5f);
        Buffer spawnBuff(1000);
        Buffer::Writer spawnWriter(&spawnBuff);
        sendSpawn.Serialize(spawnWriter);
        Buffer::Reader spawnReader(&spawnBuff);
        spawnReader.ReadBits(trash, 8);
        recvSpawn.DeserializeRaw(spawnReader);
        REQUIRE(sendSpawn == recvSpawn);
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
        move.Teleported = true;
        move.Lying = true;

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
        record.Items.Entries = {sword, gold};
        record.Cell = GameId(0, 0x31D2E);
        record.WorldSpace = GameId(0, 0x3C);
        record.Position = {1024.5f, -2048.25f, 64.f};
        roundTrip(request, RequestMessageFactory{});
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
        chunk.Offset = 65536;
        chunk.Total = 3'500'000;
        chunk.Data = std::string(NotifySaveChunk::kMaxData, '\x7F');
        roundTrip(chunk, NotifyMessageFactory{});
    }

    SECTION("The join request names the player and whether it joins from the main menu")
    {
        AuthenticationRequest request;
        request.Version = "v";
        request.PlayerKey = "id:0123456789abcdef";
        request.WantsWorld = true;
        roundTrip(request, RequestMessageFactory{});
    }

    SECTION("A player joining now is marked, and a player going down carries its name and its killer's")
    {
        NotifyPlayerJoined joined;
        joined.PlayerId = 3;
        joined.Username = "Serana";
        joined.Level = 5;
        joined.Arrived = true;
        roundTrip(joined, NotifyMessageFactory{});
        NotifyPlayerPerished perished;
        perished.PlayerId = 3;
        perished.Username = "Serana";
        roundTrip(perished, NotifyMessageFactory{});
        perished.KillerName = "Lydia";
        roundTrip(perished, NotifyMessageFactory{});
    }
    SECTION("A player's fall, the party's death state, a respawn and a wipe's reload carry who, which fall, its killer and where")
    {
        PlayerDeathRequest down;
        down.SessionId = 7;
        down.Generation = 2;
        down.Down = true;
        down.KillerId = 9;
        roundTrip(down, RequestMessageFactory{});
        NotifyPartyDeath party;
        party.Players = {{7, 2, true}, {9, 0, false}};
        party.Combat = true;
        party.RemainingMs = 4500;
        party.Error = "No previous gameplay save";
        roundTrip(party, NotifyMessageFactory{});
        NotifyPlayerRespawn respawn;
        respawn.SessionId = 7;
        respawn.Generation = 2;
        respawn.AnchorId = 9;
        respawn.CellId = GameId(0, 0x3C);
        respawn.WorldSpaceId = GameId(0, 0x3C);
        respawn.Position = {100.f, -200.f, 50.f};
        respawn.Heading = 1.5f;
        roundTrip(respawn, NotifyMessageFactory{});
        roundTrip(NotifyPartyReload{}, NotifyMessageFactory{});
    }
}

TEST_CASE("Physics bodies", "[encoding.physics]")
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

    BodyFrame frame;
    frame.Key = {.Object = GameId(0, 0x1A0F3), .Index = 2};
    frame.Position = {-182345.53f, 90211.2f, -1024.03f};
    frame.Rotation = glm::normalize(glm::quat(0.3f, -0.6f, 0.2f, 0.7f));
    frame.LinearVelocity = {120.4f, -310.9f, -686.f};
    frame.AngularVelocity = {3.1f, -0.25f, 12.f};
    frame.Gravity = true;

    SECTION("A frame arrives as its quantized self, within a millimetre and a tenth of a degree")
    {
        NotifyBodyFrames frames;
        frames.Tick = 123456789;
        frames.Frames = {frame, BodyFrame{.Key = {.Actor = 0x22, .Index = 14}, .Knocked = true}};
        const auto cDecoded = roundTrip(frames);
        REQUIRE(cDecoded.Frames.size() == 2);
        CHECK(cDecoded.Frames[1].Knocked);
        CHECK(cDecoded.Tick == frames.Tick);
        CHECK(cDecoded.Frames[0] == frame.Quantized());
        CHECK(cDecoded.Frames[1] == frames.Frames[1].Quantized());
        CHECK(glm::distance(cDecoded.Frames[0].Position, frame.Position) < 1.f / 32);
        CHECK(std::abs(glm::dot(cDecoded.Frames[0].Rotation, frame.Rotation)) > std::cos(0.002f));
        CHECK(glm::distance(cDecoded.Frames[0].LinearVelocity, frame.LinearVelocity) < 0.25f);
        THEN("a quantized frame quantizes to itself") CHECK(frame.Quantized().Quantized() == frame.Quantized());
    }

    SECTION("A frame takes about BodyFrame::kWireBytes")
    {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        frame.Serialize(writer);
        CHECK(writer.Size() <= BodyFrame::kWireBytes);
    }

    SECTION("A rest goes exactly")
    {
        NotifyBodyRests rests;
        rests.Tick = 42;
        rests.Rests = {{.Key = frame.Key, .Position = frame.Position, .Rotation = frame.Rotation}};
        CHECK(roundTrip(rests) == rests);
    }

    SECTION("A guest's push, hold and release go to the host")
    {
        BodyPushRequest pushes;
        pushes.Pushes = {{.Key = frame.Key, .Impulse = {-3.5f, 120.25f, 0.5f}, .Point = frame.Position}};
        CHECK(roundTrip(pushes, RequestMessageFactory{}) == pushes);
        BodyFramesRequest held;
        held.Tick = 77;
        held.Frames = {frame};
        CHECK(roundTrip(held, RequestMessageFactory{}).Frames[0] == frame.Quantized());
        BodyReleaseRequest released;
        released.Tick = 78;
        released.Frames = {frame};
        CHECK(roundTrip(released, RequestMessageFactory{}).Frames[0] == frame.Quantized());
        BodyPoseRequest asked;
        asked.Rests = {{.Key = frame.Key, .Position = frame.Position, .Rotation = frame.Rotation}};
        CHECK(roundTrip(asked, RequestMessageFactory{}) == asked);
        BodyRestsRequest rests;
        rests.Tick = 79;
        rests.Rests = asked.Rests;
        CHECK(roundTrip(rests, RequestMessageFactory{}) == rests);
    }
}

TEST_CASE("Shared wait messages keep generation, authority and the whole calendar", "[encoding.wait]")
{
    WaitRequest request;
    request.Action = WaitAction::Progress;
    request.Id = 1234;
    request.Hours = 24;
    request.RemainingSeconds = 23 * 3600 + 900;
    request.Sleeping = true;
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    request.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = RequestMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == WaitRequest::Opcode);
    const auto& received = static_cast<const WaitRequest&>(*decoded);
    CHECK(received.Action == request.Action);
    CHECK(received.Id == request.Id);
    CHECK(received.Hours == request.Hours);
    CHECK(received.RemainingSeconds == request.RemainingSeconds);
    CHECK(received.Sleeping == request.Sleeping);

    NotifyWaitState state;
    state.Phase = WaitPhase::Progress;
    state.Id = 1234;
    state.Hours = 24;
    state.RemainingSeconds = 900;
    state.Sleeping = true;
    state.Driver = true;
    state.Calendar.Time = 0.5f;
    state.Calendar.DaysPassed = 333;
    state.Calendar.Month = 11;
    state.Calendar.Day = 31;
    state.Calendar.Year = 202;
    Buffer reply;
    Buffer::Writer replyWriter(&reply);
    state.Serialize(replyWriter);
    Buffer::Reader replyReader(&reply);
    auto notification = NotifyMessageFactory{}.Extract(replyReader);
    REQUIRE(notification);
    REQUIRE(notification->GetOpcode() == NotifyWaitState::Opcode);
    const auto& result = static_cast<const NotifyWaitState&>(*notification);
    CHECK(result.Phase == state.Phase);
    CHECK(result.Id == state.Id);
    CHECK(result.Hours == state.Hours);
    CHECK(result.RemainingSeconds == state.RemainingSeconds);
    CHECK(result.Sleeping == state.Sleeping);
    CHECK(result.Driver == state.Driver);
    CHECK(result.Calendar == state.Calendar);
}

TEST_CASE("Shared rest world messages retain the wait id and bounded cell payload", "[encoding.wait]")
{
    WaitWorldRequest request;
    request.Id = 54321;
    request.State.Cell = GameId(4, 0x31D2E);
    request.State.Changed = {{GameId(2, 0x1A002), 0x80000002}};
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    request.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = RequestMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == WaitWorldRequest::Opcode);
    const auto& received = static_cast<const WaitWorldRequest&>(*decoded);
    CHECK(received.Id == request.Id);
    CHECK(received.State == request.State);

    NotifyWaitWorld reply;
    reply.Id = request.Id;
    reply.Last = false;
    reply.State.Cell = request.State.Cell;
    reply.State.Changes = {{.Type = ObjectChange::Kind::kRecord, .Object = GameId(2, 0x1A002), .Value = 0x80000002}};
    Buffer notification;
    Buffer::Writer notificationWriter(&notification);
    reply.Serialize(notificationWriter);
    Buffer::Reader notificationReader(&notification);
    auto message = NotifyMessageFactory{}.Extract(notificationReader);
    REQUIRE(message);
    REQUIRE(message->GetOpcode() == NotifyWaitWorld::Opcode);
    const auto& result = static_cast<const NotifyWaitWorld&>(*message);
    CHECK(result.Id == reply.Id);
    CHECK_FALSE(result.Last);
    CHECK(result.State == reply.State);
}

TEST_CASE("Dropped items keep their extra data on the wire", "[encoding.items]")
{
    Inventory::Entry item;
    item.BaseId = GameId(0, 0x12EB7);
    item.Count = 1;
    item.ExtraHealth = 1.3f;
    item.ExtraEnchantId = GameId(0, 0x4605A);
    item.ExtraEnchantCharge = 500;
    item.ExtraName = "Old Friend";

    DropItemRequest drop;
    drop.Tag = 0xFF000A31;
    drop.Item = item;
    drop.Placement = {GameId(4, 0x31D2E), {}, {12.f, -40.5f, 64.f}, {0.f, 0.f, 3.1f}};
    PickUpRequest pickUp;
    pickUp.Object = GameId(ObjectChange::kCreatedModId, 0xA31);
    pickUp.Tag = 0xFF000A31;
    for (const RequestMessage* pSent : std::initializer_list<const RequestMessage*>{&drop, &pickUp})
    {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        pSent->Serialize(writer);
        Buffer::Reader reader(&buffer);
        auto decoded = RequestMessageFactory{}.Extract(reader);
        REQUIRE(decoded);
        REQUIRE(decoded->GetOpcode() == pSent->GetOpcode());
        if (pSent == &drop)
            CHECK(static_cast<const DropItemRequest&>(*decoded) == drop);
        else
            CHECK(static_cast<const PickUpRequest&>(*decoded) == pickUp);
    }

    NotifyItemDropped dropped;
    dropped.Tag = drop.Tag;
    dropped.Object = pickUp.Object;
    NotifyPickUp picked;
    picked.Object = pickUp.Object;
    picked.Item = item;
    for (const NotifyMessage* pSent : std::initializer_list<const NotifyMessage*>{&dropped, &picked})
    {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        pSent->Serialize(writer);
        Buffer::Reader reader(&buffer);
        auto decoded = NotifyMessageFactory{}.Extract(reader);
        REQUIRE(decoded);
        REQUIRE(decoded->GetOpcode() == pSent->GetOpcode());
        if (pSent == &dropped)
            CHECK(static_cast<const NotifyItemDropped&>(*decoded) == dropped);
        else
            CHECK(static_cast<const NotifyPickUp&>(*decoded) == picked);
    }
}

TEST_CASE("The party's gold goes to every game", "[encoding.items]")
{
    NotifyPartyGold sent;
    sent.Gold = 123456;
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = NotifyMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == kNotifyPartyGold);
    CHECK(static_cast<const NotifyPartyGold&>(*decoded) == sent);
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

TEST_CASE("Party travel preserves queued destinations and native travel hours", "[encoding.travel]")
{
    const auto roundTrip = [](const auto& acMessage) {
        Buffer buffer;
        Buffer::Writer writer(&buffer);
        acMessage.Serialize(writer);
        Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
        auto decoded = RequestMessageFactory{}.Extract(reader);
        REQUIRE(decoded);
        using Message = std::remove_cvref_t<decltype(acMessage)>;
        CHECK(*CastUnique<Message>(std::move(decoded)) == acMessage);
        CHECK(reader.GetBitPosition() == writer.GetBitPosition());
    };
    TravelTimeRequest time;
    time.Hours = 27.25f;
    roundTrip(time);
    PartyTravelRequest travel;
    travel.Destination.CellId = GameId(1, 0xABC);
    travel.Destination.WorldSpaceId = GameId(2, 0x123);
    travel.Destination.Position = {30.f, -40.f, 50.f};
    travel.Destination.Heading = 2.1f;
    roundTrip(travel);
}

TEST_CASE("Dismount carries the horse's placement to its next owner", "[encoding.travel]")
{
    DismountRequest dismount;
    dismount.RiderId = 8;
    dismount.MountId = 9;
    dismount.Whereabouts.Where = ActorWhereabouts::State::kPlaced;
    dismount.Whereabouts.CellId = GameId(0, 0x9C5E1);
    dismount.Whereabouts.WorldSpaceId = GameId(0, 0x3C);
    dismount.Whereabouts.Position = {100.f, 200.f, -30.f};
    dismount.Whereabouts.Heading = 0.5f;
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    dismount.Serialize(writer);
    Buffer::Reader reader(std::span(buffer.GetData(), writer.Size()));
    auto message = RequestMessageFactory{}.Extract(reader);
    REQUIRE(message);
    CHECK(*CastUnique<DismountRequest>(std::move(message)) == dismount);
}

TEST_CASE("Follower trade grants and releases retain the NPC session id", "[encoding.followers]")
{
    NotifyFollowerTrade grant;
    grant.NpcId = 98765;
    Buffer buffer;
    Buffer::Writer writer(&buffer);
    grant.Serialize(writer);
    Buffer::Reader reader(&buffer);
    auto decoded = NotifyMessageFactory{}.Extract(reader);
    REQUIRE(decoded);
    REQUIRE(decoded->GetOpcode() == NotifyFollowerTrade::Opcode);
    CHECK(static_cast<const NotifyFollowerTrade&>(*decoded).NpcId == grant.NpcId);
    FollowerTradeClosedRequest close;
    close.NpcId = grant.NpcId;
    Buffer request;
    Buffer::Writer requestWriter(&request);
    close.Serialize(requestWriter);
    Buffer::Reader requestReader(&request);
    auto received = RequestMessageFactory{}.Extract(requestReader);
    REQUIRE(received);
    REQUIRE(received->GetOpcode() == FollowerTradeClosedRequest::Opcode);
    CHECK(static_cast<const FollowerTradeClosedRequest&>(*received).NpcId == close.NpcId);
}
