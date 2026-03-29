#include <catch2/catch.hpp>
#include "json_protocol.h"
#include "nlohmann/json.hpp"

#include <string>

using json = nlohmann::json;

TEST_CASE("Hook op names are snake_case", "[JsonProtocol]")
{
    REQUIRE(std::string(JsonOp::kActorAdded) == "actor_added");
    REQUIRE(std::string(JsonOp::kActorRemoved) == "actor_removed");
    REQUIRE(std::string(JsonOp::kDamageActor) == "damage_actor");
    REQUIRE(std::string(JsonOp::kSetPosition) == "set_position");
    REQUIRE(std::string(JsonOp::kSpellCast) == "spell_cast");
    REQUIRE(std::string(JsonOp::kAddInventory) == "add_inventory");
    REQUIRE(std::string(JsonOp::kEquip) == "equip");
    REQUIRE(std::string(JsonOp::kWeatherChange) == "weather_change");
    REQUIRE(std::string(JsonOp::kUnequip) == "unequip");
    REQUIRE(std::string(JsonOp::kActivate) == "activate");
    REQUIRE(std::string(JsonOp::kLockChange) == "lock_change");
    REQUIRE(std::string(JsonOp::kProjectile) == "projectile");
    REQUIRE(std::string(JsonOp::kActorProcess) == "actor_process");
    REQUIRE(std::string(JsonOp::kCharacterCtor) == "character_ctor");
    REQUIRE(std::string(JsonOp::kSpawnActor) == "spawn_actor");
    REQUIRE(std::string(JsonOp::kDeathItems) == "death_items");
    REQUIRE(std::string(JsonOp::kRegenAttr) == "regen_attr");
    REQUIRE(std::string(JsonOp::kApplyEffect) == "apply_effect");
    REQUIRE(std::string(JsonOp::kPerformAction) == "perform_action");
    REQUIRE(std::string(JsonOp::kRemoveSpell) == "remove_spell");
    REQUIRE(std::string(JsonOp::kPickUp) == "pick_up");
    REQUIRE(std::string(JsonOp::kDropObject) == "drop_object");
    REQUIRE(std::string(JsonOp::kAddTarget) == "add_target");
    REQUIRE(std::string(JsonOp::kInterruptCast) == "interrupt_cast");
    REQUIRE(std::string(JsonOp::kSimulateTime) == "simulate_time");
    REQUIRE(std::string(JsonOp::kSetWeather) == "set_weather");
    REQUIRE(std::string(JsonOp::kForceWeather) == "force_weather");
    REQUIRE(std::string(JsonOp::kRemoveInvItem) == "remove_inv_item");
    REQUIRE(std::string(JsonOp::kPlayAnimation) == "play_animation");
    REQUIRE(std::string(JsonOp::kMountPackage) == "mount_package");
}

TEST_CASE("Command op names have cmd_ prefix", "[JsonProtocol]")
{
    auto hasPrefix = [](const char* s) {
        return std::string(s).substr(0, 4) == "cmd_";
    };

    REQUIRE(hasPrefix(JsonOp::kCmdSetPosition));
    REQUIRE(hasPrefix(JsonOp::kCmdForceWeather));
    REQUIRE(hasPrefix(JsonOp::kCmdCastSpell));
    REQUIRE(hasPrefix(JsonOp::kCmdEquipItem));
    REQUIRE(hasPrefix(JsonOp::kCmdUnequipItem));
    REQUIRE(hasPrefix(JsonOp::kCmdPlayAnimation));
    REQUIRE(hasPrefix(JsonOp::kCmdSetActorValue));
    REQUIRE(hasPrefix(JsonOp::kCmdDamageActor));
    REQUIRE(hasPrefix(JsonOp::kCmdAddSpell));
    REQUIRE(hasPrefix(JsonOp::kCmdRemoveSpell));
    REQUIRE(hasPrefix(JsonOp::kCmdAddItem));
    REQUIRE(hasPrefix(JsonOp::kCmdRemoveItem));
    REQUIRE(hasPrefix(JsonOp::kCmdActivate));
    REQUIRE(hasPrefix(JsonOp::kCmdLockChange));
    REQUIRE(hasPrefix(JsonOp::kCmdLaunchProjectile));
}

TEST_CASE("Control op names have ctrl_ prefix", "[JsonProtocol]")
{
    auto hasPrefix = [](const char* s) {
        return std::string(s).substr(0, 5) == "ctrl_";
    };

    REQUIRE(hasPrefix(JsonOp::kCtrlHeartbeat));
    REQUIRE(hasPrefix(JsonOp::kCtrlShutdown));
    REQUIRE(hasPrefix(JsonOp::kCtrlReady));
}

TEST_CASE("SetPosition JSON round-trip", "[JsonProtocol]")
{
    json msg;
    msg[JsonField::kOp] = JsonOp::kSetPosition;
    msg[JsonField::kFormId] = 0x14u;
    msg[JsonField::kPtr] = 0xDEADBEEFull;
    msg[JsonField::kX] = 100.5f;
    msg[JsonField::kY] = 200.25f;
    msg[JsonField::kZ] = 300.75f;
    msg[JsonField::kRx] = 1.0f;
    msg[JsonField::kRy] = 2.0f;
    msg[JsonField::kRz] = 3.0f;

    std::string serialized = msg.dump();
    json parsed = json::parse(serialized);

    REQUIRE(parsed[JsonField::kOp] == JsonOp::kSetPosition);
    REQUIRE(parsed[JsonField::kFormId] == 0x14u);
    REQUIRE(parsed[JsonField::kPtr] == 0xDEADBEEFull);
    REQUIRE(parsed[JsonField::kX].get<float>() == Approx(100.5f));
    REQUIRE(parsed[JsonField::kY].get<float>() == Approx(200.25f));
    REQUIRE(parsed[JsonField::kZ].get<float>() == Approx(300.75f));
    REQUIRE(parsed[JsonField::kRx].get<float>() == Approx(1.0f));
    REQUIRE(parsed[JsonField::kRy].get<float>() == Approx(2.0f));
    REQUIRE(parsed[JsonField::kRz].get<float>() == Approx(3.0f));
}

TEST_CASE("CmdSetPosition JSON round-trip", "[JsonProtocol]")
{
    json msg;
    msg[JsonField::kOp] = JsonOp::kCmdSetPosition;
    msg[JsonField::kActorPtr] = 0xCAFEBABEull;
    msg[JsonField::kX] = 500.0f;
    msg[JsonField::kY] = 600.0f;
    msg[JsonField::kZ] = 700.0f;

    std::string serialized = msg.dump();
    json parsed = json::parse(serialized);

    REQUIRE(parsed[JsonField::kOp] == JsonOp::kCmdSetPosition);
    REQUIRE(parsed[JsonField::kActorPtr] == 0xCAFEBABEull);
    REQUIRE(parsed[JsonField::kX].get<float>() == Approx(500.0f));
    REQUIRE(parsed[JsonField::kY].get<float>() == Approx(600.0f));
    REQUIRE(parsed[JsonField::kZ].get<float>() == Approx(700.0f));
}

TEST_CASE("CtrlHeartbeat JSON round-trip", "[JsonProtocol]")
{
    json msg;
    msg[JsonField::kOp] = JsonOp::kCtrlHeartbeat;

    std::string serialized = msg.dump();
    json parsed = json::parse(serialized);

    REQUIRE(parsed[JsonField::kOp] == JsonOp::kCtrlHeartbeat);
    REQUIRE(parsed.size() == 1);
}
