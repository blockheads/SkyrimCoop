#pragma once

// JSON Wire Protocol — shared op name and field name constants.
// Pure constants header: no nlohmann/json dependency.
// Matches binary opcodes in protocol.h 1:1.

namespace JsonOp {

// Hook events (DLL -> Native): 30 ops
constexpr const char* kActorAdded    = "actor_added";
constexpr const char* kActorRemoved  = "actor_removed";
constexpr const char* kDamageActor   = "damage_actor";
constexpr const char* kSetPosition   = "set_position";
constexpr const char* kSpellCast     = "spell_cast";
constexpr const char* kAddInventory  = "add_inventory";
constexpr const char* kEquip         = "equip";
constexpr const char* kWeatherChange = "weather_change";
constexpr const char* kUnequip       = "unequip";
constexpr const char* kActivate      = "activate";
constexpr const char* kLockChange    = "lock_change";
constexpr const char* kProjectile    = "projectile";
constexpr const char* kActorProcess  = "actor_process";
constexpr const char* kCharacterCtor = "character_ctor";
constexpr const char* kSpawnActor    = "spawn_actor";
constexpr const char* kDeathItems    = "death_items";
constexpr const char* kRegenAttr     = "regen_attr";
constexpr const char* kApplyEffect   = "apply_effect";
constexpr const char* kPerformAction = "perform_action";
constexpr const char* kRemoveSpell   = "remove_spell";
constexpr const char* kPickUp        = "pick_up";
constexpr const char* kDropObject    = "drop_object";
constexpr const char* kAddTarget     = "add_target";
constexpr const char* kInterruptCast = "interrupt_cast";
constexpr const char* kSimulateTime  = "simulate_time";
constexpr const char* kSetWeather    = "set_weather";
constexpr const char* kForceWeather  = "force_weather";
constexpr const char* kRemoveInvItem = "remove_inv_item";
constexpr const char* kPlayAnimation = "play_animation";
constexpr const char* kMountPackage  = "mount_package";

// Command ops (Native -> DLL): 15 ops
constexpr const char* kCmdSetPosition      = "cmd_set_position";
constexpr const char* kCmdForceWeather     = "cmd_force_weather";
constexpr const char* kCmdCastSpell        = "cmd_cast_spell";
constexpr const char* kCmdEquipItem        = "cmd_equip_item";
constexpr const char* kCmdUnequipItem      = "cmd_unequip_item";
constexpr const char* kCmdPlayAnimation    = "cmd_play_animation";
constexpr const char* kCmdSetActorValue    = "cmd_set_actor_value";
constexpr const char* kCmdDamageActor      = "cmd_damage_actor";
constexpr const char* kCmdAddSpell         = "cmd_add_spell";
constexpr const char* kCmdRemoveSpell      = "cmd_remove_spell";
constexpr const char* kCmdAddItem          = "cmd_add_item";
constexpr const char* kCmdRemoveItem       = "cmd_remove_item";
constexpr const char* kCmdActivate         = "cmd_activate";
constexpr const char* kCmdLockChange       = "cmd_lock_change";
constexpr const char* kCmdLaunchProjectile = "cmd_launch_projectile";

// Actor spawning/removal ops (Native -> DLL): 2 ops (Phase 15)
constexpr const char* kCmdSpawnActor  = "cmd_spawn_actor";
constexpr const char* kCmdRemoveActor = "cmd_remove_actor";

// Spawn result event (DLL -> Native): 1 op (Phase 15)
constexpr const char* kSpawnResult = "spawn_result";

// Control ops (bidirectional): 3 ops
constexpr const char* kCtrlHeartbeat = "ctrl_heartbeat";
constexpr const char* kCtrlShutdown  = "ctrl_shutdown";
constexpr const char* kCtrlReady     = "ctrl_ready";

} // namespace JsonOp

namespace JsonField {

// Common fields
constexpr const char* kOp     = "op";
constexpr const char* kFormId = "form_id";
constexpr const char* kPtr    = "ptr";

// Position / rotation
constexpr const char* kX  = "x";
constexpr const char* kY  = "y";
constexpr const char* kZ  = "z";
constexpr const char* kRx = "rx";
constexpr const char* kRy = "ry";
constexpr const char* kRz = "rz";

// Damage / delta
constexpr const char* kDamage = "damage";
constexpr const char* kDelta  = "delta";

// Actor fields
constexpr const char* kActorFormId = "actor_form_id";
constexpr const char* kActorPtr    = "actor_ptr";

// Caster fields
constexpr const char* kCasterFormId = "caster_form_id";
constexpr const char* kCasterPtr    = "caster_ptr";

// Spell fields
constexpr const char* kSpellFormId = "spell_form_id";
constexpr const char* kSpellPtr    = "spell_ptr";

// Item fields
constexpr const char* kItemFormId = "item_form_id";
constexpr const char* kItemPtr    = "item_ptr";
constexpr const char* kCount      = "count";
constexpr const char* kRefPtr     = "ref_ptr";

// Object fields
constexpr const char* kObjFormId = "obj_form_id";
constexpr const char* kObjPtr    = "obj_ptr";

// Activator fields
constexpr const char* kActivatorFormId = "activator_form_id";
constexpr const char* kActivatorPtr    = "activator_ptr";

// Lock fields
constexpr const char* kLocked = "locked";

// Data pointer
constexpr const char* kDataPtr = "data_ptr";

// Weather fields
constexpr const char* kSkyPtr        = "sky_ptr";
constexpr const char* kWeatherFormId = "weather_form_id";
constexpr const char* kWeatherPtr    = "weather_ptr";
constexpr const char* kOverride      = "override";
constexpr const char* kAccel         = "accel";

// This-pointer fields
constexpr const char* kThisPtr    = "this_ptr";
constexpr const char* kThisFormId = "this_form_id";

// Target fields
constexpr const char* kTargetFormId = "target_form_id";
constexpr const char* kTargetPtr    = "target_ptr";
constexpr const char* kHitDataPtr   = "hit_data_ptr";

// Spell interrupt
constexpr const char* kRefund = "refund";

// Regen / actor value
constexpr const char* kType  = "type";
constexpr const char* kValue = "value";

// Effect
constexpr const char* kEffectPtr = "effect_ptr";

// Process
constexpr const char* kProcessPtr = "process_ptr";

// Action fields
constexpr const char* kActionFormId = "action_form_id";
constexpr const char* kActionPtr    = "action_ptr";
constexpr const char* kActionId     = "action_id";

// Animation
constexpr const char* kAnimPtr = "anim_ptr";

// Mount
constexpr const char* kMountFormId = "mount_form_id";
constexpr const char* kMountPtr    = "mount_ptr";

// Actor value index
constexpr const char* kAvIndex = "av_index";

// Spawn result fields (Phase 15)
constexpr const char* kSuccess = "success";

} // namespace JsonField
