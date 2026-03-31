/// ReplayBridge.cpp — Headless replay implementation.
///
/// IMPORTANT: This file must NOT include any SKSE header.
/// All methods either return safe defaults or look up a canned result from
/// m_queryResponses (populated by LoadRecording from an NDJSON recording file).

#include <Bridges/ReplayBridge.h>

#include <Structs/ActorValues.h>
#include <Structs/AnimationVariables.h>
#include <Structs/Faction.h>
#include <Structs/Inventory.h>
#include <Structs/Tints.h>
#include <Structs/ActionEvent.h>
#include <Structs/QuestLog.h>

#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// LoadRecording
// ---------------------------------------------------------------------------

void ReplayBridge::LoadRecording(const std::string& aPath) noexcept
{
    m_queryResponses.clear();
    m_eventEntries.clear();

    std::ifstream file(aPath);
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        try
        {
            nlohmann::json j = nlohmann::json::parse(line);

            const std::string dir = j.value("dir", "");
            const std::string op  = j.value("op", "");
            if (op.empty())
                continue;

            if (dir == "return")
            {
                // Store canned query responses keyed by op name (FIFO order)
                m_queryResponses[op].push_back(std::move(j));
            }
            else if (dir == "call")
            {
                // Event/mutation calls recorded for PumpEvents()
                m_eventEntries.push_back(std::move(j));
            }
        }
        catch (...)
        {
            // Skip malformed lines silently
        }
    }
}

// ---------------------------------------------------------------------------
// NextResponse — pop the next canned response for an op (FIFO)
// ---------------------------------------------------------------------------

nlohmann::json ReplayBridge::NextResponse(const char* apOp) const noexcept
{
    auto it = m_queryResponses.find(apOp);
    if (it == m_queryResponses.end() || it->second.empty())
        return nlohmann::json{};

    nlohmann::json result = it->second.front();
    it->second.erase(it->second.begin());
    return result;
}

// ---------------------------------------------------------------------------
// PumpEvents — fire registered callbacks for recorded event entries
// ---------------------------------------------------------------------------

void ReplayBridge::PumpEvents() noexcept
{
    for (const auto& entry : m_eventEntries)
    {
        const std::string op = entry.value("op", "");

        if (op == "QuestStartStop" && m_questStartStopCb)
        {
            uint32_t questId = entry.value("formId", 0u);
            bool started = entry.value("started", false);
            m_questStartStopCb(questId, started);
        }
        else if (op == "QuestStage" && m_questStageCb)
        {
            uint32_t questId = entry.value("formId", 0u);
            uint32_t stage = entry.value("stage", 0u);
            m_questStageCb(questId, stage);
        }
        else if (op == "Activate" && m_activateCb)
        {
            uint32_t activator = entry.value("activatorFormId", 0u);
            uint32_t activated = entry.value("activatedFormId", 0u);
            m_activateCb(activator, activated);
        }
        else if (op == "LockChange" && m_lockChangeCb)
        {
            uint32_t objectId = entry.value("formId", 0u);
            bool locked = entry.value("locked", false);
            m_lockChangeCb(objectId, locked);
        }
    }
}

// ---------------------------------------------------------------------------
// IActorBridge — query methods use NextResponse for canned values
// ---------------------------------------------------------------------------

bool ReplayBridge::ActorExists(uint32_t aFormId) const noexcept
{
    auto r = NextResponse("ActorExists");
    if (r.contains("result")) return r["result"].get<bool>();
    return false;
}
bool ReplayBridge::IsActorDead(uint32_t aFormId) const noexcept
{
    auto r = NextResponse("IsActorDead");
    if (r.contains("result")) return r["result"].get<bool>();
    return false;
}
bool ReplayBridge::IsPlayerSummon(uint32_t aFormId) const noexcept { return false; }
bool ReplayBridge::IsTemporaryActor(uint32_t aFormId) const noexcept { return false; }
bool ReplayBridge::IsRemotePlayer(uint32_t aFormId) const noexcept
{
    auto r = NextResponse("IsRemotePlayer");
    if (r.contains("result")) return r["result"].get<bool>();
    return false;
}
void ReplayBridge::SetActorRemote(uint32_t, bool) noexcept {}
void ReplayBridge::SetActorPlayer(uint32_t, bool) noexcept {}
uint32_t ReplayBridge::GetCommandingActor(uint32_t) const noexcept { return 0; }
void ReplayBridge::KillActor(uint32_t) noexcept {}
void ReplayBridge::RespawnActor(uint32_t) noexcept {}
void ReplayBridge::DeleteActor(uint32_t) noexcept {}
void ReplayBridge::MoveActorTo(uint32_t, uint32_t, float, float, float) noexcept {}
void ReplayBridge::SetActorRotation(uint32_t, float, float) noexcept {}
void ReplayBridge::GetActorPosition(uint32_t, float& aX, float& aY, float& aZ) const noexcept
    { aX = 0.f; aY = 0.f; aZ = 0.f; }
void ReplayBridge::GetActorRotation(uint32_t, float& aX, float& aY, float& aZ) const noexcept
    { aX = 0.f; aY = 0.f; aZ = 0.f; }
float ReplayBridge::GetActorValue(uint32_t, int32_t) const noexcept { return 0.f; }
float ReplayBridge::GetActorPermanentValue(uint32_t, int32_t) const noexcept { return 0.f; }
void ReplayBridge::SetActorValue(uint32_t, int32_t, float) noexcept {}
void ReplayBridge::ForceActorValue(uint32_t, int32_t, int32_t, float) noexcept {}
int32_t ReplayBridge::GetActorValueCount() const noexcept { return 164; } // kActorValueCount
void ReplayBridge::EquipItem(uint32_t, uint32_t, bool) noexcept {}
void ReplayBridge::UnequipItem(uint32_t, uint32_t) noexcept {}
void ReplayBridge::DrawWeapon(uint32_t, bool) noexcept {}
bool ReplayBridge::IsWeaponDrawn(uint32_t) const noexcept { return false; }
uint32_t ReplayBridge::GetEquippedWeaponFormId(uint32_t, uint32_t) const noexcept { return 0; }
uint64_t ReplayBridge::GetAnimationGraphDescriptorHash(uint32_t) const noexcept { return 0; }
void ReplayBridge::SetAnimationGraphDescriptorHash(uint32_t, uint64_t) noexcept {}
void ReplayBridge::GenerateMagicCasters(uint32_t) noexcept {}
void ReplayBridge::SetMagicCasterDualCasting(uint32_t, uint32_t, bool) noexcept {}
void ReplayBridge::CastSpell(uint32_t, uint32_t, uint32_t) noexcept {}
void ReplayBridge::InterruptCast(uint32_t, uint32_t) noexcept {}
void ReplayBridge::RemoveSpell(uint32_t, uint32_t) noexcept {}
void ReplayBridge::AddMagicTarget(uint32_t, uint32_t, uint32_t, bool, bool) noexcept {}
bool ReplayBridge::IsBleedingOut(uint32_t) const noexcept { return false; }
void ReplayBridge::PayCrimeGoldToAllFactions(uint32_t) noexcept {}
uint32_t ReplayBridge::GetEquippedShoutFormId(uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::GetActiveMagicItemFormId(uint32_t, uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::GetCombatTargetFormId(uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::GetActorBaseFormId(uint32_t) const noexcept { return 0; }
std::string ReplayBridge::GetActorBaseName(uint32_t) const noexcept { return {}; }
uint32_t ReplayBridge::GetActorRaceFormId(uint32_t) const noexcept { return 0; }
bool ReplayBridge::ActorHas3D(uint32_t) const noexcept { return false; }
// --- IActorBridge extended (Plan 03) ---
ActorValues ReplayBridge::GetActorEssentialValues(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetActorValues(uint32_t, const ActorValues&) noexcept {}
Inventory ReplayBridge::GetActorInventory(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetActorInventory(uint32_t, const Inventory&) noexcept {}
Factions ReplayBridge::GetActorFactions(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetActorFactions(uint32_t, const Factions&) noexcept {}
void ReplayBridge::SetActorWeaponDrawnEx(uint32_t, bool) noexcept {}
bool ReplayBridge::IsWeaponFullyDrawn(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsActorTemporary(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsActorMount(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsActorDragon(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsActorVampireLord(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsActorDisabled(uint32_t) const noexcept { return false; }
void ReplayBridge::EnableActor(uint32_t) noexcept {}
void ReplayBridge::ActorSpeakSound(uint32_t, const std::string&) noexcept {}
void ReplayBridge::ActorStopCurrentDialogue(uint32_t) noexcept {}
void ReplayBridge::SetActorIgnoreFriendlyHit(uint32_t, bool) noexcept {}
void ReplayBridge::SetActorPlayerRespawnMode(uint32_t) noexcept {}
void ReplayBridge::SetActorCommandingActor(uint32_t, uint32_t) noexcept {}
uint32_t ReplayBridge::GetActorCommandingActorFormId(uint32_t) const noexcept { return 0; }
void ReplayBridge::SetActorPackage(uint32_t, uint32_t) noexcept {}
void ReplayBridge::ActorLoadAnimationVariables(uint32_t, const AnimationVariables&) noexcept {}
void ReplayBridge::ActorSaveAnimationVariables(uint32_t, AnimationVariables&) const noexcept {}
ActionEvent ReplayBridge::GetActorLatestAnimation(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetActorLatestAnimation(uint32_t, const ActionEvent&) noexcept {}
void ReplayBridge::ActorFixVampireLordModel(uint32_t) noexcept {}
void ReplayBridge::ActorInitiateMountPackage(uint32_t, uint32_t) noexcept {}
uint32_t ReplayBridge::GetActorParentCellFormId(uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::GetActorWorldSpaceFormId(uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::SpawnActorFromNpc(uint32_t) noexcept { return 0; }
uint32_t ReplayBridge::CreateCustomActor(const TiltedPhoques::String&, uint32_t) noexcept { return 0; }
void ReplayBridge::DeserializeActorNpc(uint32_t, const TiltedPhoques::String&, uint32_t) noexcept {}
uint32_t ReplayBridge::SerializeActorNpc(uint32_t, TiltedPhoques::String&) noexcept { return 0; }
void ReplayBridge::MarkActorNpcChanged(uint32_t, uint32_t) noexcept {}
uint32_t ReplayBridge::GetActorNpcTemplateBaseFormId(uint32_t) const noexcept { return 0; }
bool ReplayBridge::IsActorNpcTemporary(uint32_t) const noexcept { return false; }
Tints ReplayBridge::GetActorFaceTints(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetupActorFaceGen(uint32_t) noexcept {}
void ReplayBridge::SetupActorCache(uint32_t, uint32_t) noexcept {}
void ReplayBridge::ActorUpdateInterpolation(uint32_t, uint64_t) noexcept {}
void ReplayBridge::ActorUpdateAnimation(uint32_t, uint64_t) noexcept {}
void ReplayBridge::ActorUpdateFaceGen(uint32_t) noexcept {}
void ReplayBridge::ApplyRevealEffect(uint32_t, float) noexcept {}
void ReplayBridge::PlayerKnockExplosion(uint32_t, float, float, float, float) noexcept {}

// ---------------------------------------------------------------------------
// IFormBridge — stub returns
// ---------------------------------------------------------------------------

bool ReplayBridge::FormExists(uint32_t) const noexcept { return false; }
uint8_t ReplayBridge::GetFormType(uint32_t) const noexcept { return 0; }
std::string ReplayBridge::GetFormEditorId(uint32_t) const noexcept { return {}; }
std::string ReplayBridge::GetFormName(uint32_t) const noexcept { return {}; }
float ReplayBridge::GetGlobalValue(uint32_t) const noexcept { return 0.f; }
void ReplayBridge::SetGlobalValue(uint32_t, float) noexcept {}
uint32_t ReplayBridge::LookupCellByCoords(uint32_t, int32_t, int32_t) const noexcept { return 0; }
uint32_t ReplayBridge::LookupWorldSpaceByFormId(uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::GetParentCellFormId(uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::GetParentWorldSpaceFormId(uint32_t) const noexcept { return 0; }
bool ReplayBridge::QuestExists(uint32_t) const noexcept { return false; }
uint32_t ReplayBridge::GetQuestStage(uint32_t) const noexcept { return 0; }
bool ReplayBridge::IsQuestStopped(uint32_t) const noexcept { return false; }
uint8_t ReplayBridge::GetQuestType(uint32_t) const noexcept { return 0; }
std::string ReplayBridge::GetQuestName(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetQuestActive(uint32_t, bool) noexcept {}
uint32_t ReplayBridge::FindQuestByNameId(const std::string&) const noexcept { return 0; }
uint32_t ReplayBridge::GetModCount() const noexcept { return 0; }
std::string ReplayBridge::GetModNameByIndex(uint32_t) const noexcept { return {}; }
uint32_t ReplayBridge::GetModStandardId(const std::string&) const noexcept { return 0; }
std::vector<std::string> ReplayBridge::GetAllModNames() const noexcept { return {}; }
uint32_t ReplayBridge::GetModFormIdBySpellId(uint32_t aModStandardId, uint32_t aSpellLocalId) const noexcept
    { return (aModStandardId << 24) | aSpellLocalId; }
bool ReplayBridge::IsSpellItem(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsConcentrationSpell(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsHealingSpell(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsWardSpell(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsInvisibilitySpell(uint32_t) const noexcept { return false; }
bool ReplayBridge::IsMagicItem(uint32_t) const noexcept { return false; }
// Quest methods added in Plan 02
void ReplayBridge::ScriptSetQuestStage(uint32_t, uint32_t) noexcept {}
void ReplayBridge::SetQuestStopped(uint32_t) noexcept {}
bool ReplayBridge::IsQuestStagesEmpty(uint32_t) const noexcept { return true; }
bool ReplayBridge::IsQuestOverrideActive() const noexcept { return false; }
std::vector<IFormBridge::ModEntry> ReplayBridge::GetLoadedMods() const noexcept { return {}; }
// Object operations (Plan 02.5-03)
bool ReplayBridge::ObjectHasLock(uint32_t) const noexcept { return false; }
bool ReplayBridge::ObjectIsLocked(uint32_t) const noexcept { return false; }
uint8_t ReplayBridge::GetObjectLockLevel(uint32_t) const noexcept { return 0; }
bool ReplayBridge::SetObjectLock(uint32_t, bool, uint8_t) noexcept { return false; }
void ReplayBridge::NotifyObjectLockChange(uint32_t) noexcept {}
uint32_t ReplayBridge::GetObjectParentCellId(uint32_t) const noexcept { return 0; }
void ReplayBridge::ActivateObjectFromActor(uint32_t, uint32_t, uint8_t) noexcept {}
void ReplayBridge::PlayObjectAnimation(uint32_t, const std::string&, const std::string&) noexcept {}
uint8_t ReplayBridge::GetObjectOpenState(uint32_t) const noexcept { return 0; }
bool ReplayBridge::IsObjectBaseFormType(uint32_t, uint8_t) const noexcept { return false; }
Inventory ReplayBridge::GetObjectInventory(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetObjectInventoryRetainingQuestItems(uint32_t, const Inventory&, const Inventory&) noexcept {}
void ReplayBridge::SetObjectInventory(uint32_t, const Inventory&) noexcept {}
bool ReplayBridge::IsRightHandEquipSlot(uint32_t) const noexcept { return false; }
void ReplayBridge::LaunchProjectile(uint32_t, uint32_t, float, float, float, uint32_t, uint32_t, uint32_t,
                                     uint32_t, uint32_t, float, float, float, int32_t, float, float,
                                     bool, bool, bool, bool, bool, bool, bool) noexcept {}
// Actor inventory/equipment helpers (Plan 02.5-03)
Inventory ReplayBridge::GetActorEquipment(uint32_t) const noexcept { return {}; }
void ReplayBridge::ActorDropOrPickUpObject(uint32_t, const Inventory::Entry&) noexcept {}
void ReplayBridge::ObjectAddOrRemoveItem(uint32_t, const Inventory::Entry&) noexcept {}
Inventory ReplayBridge::GetActorWornArmor(uint32_t) const noexcept { return {}; }
bool ReplayBridge::ActorIsWearingBodyPiece(uint32_t) const noexcept { return true; }
bool ReplayBridge::ActorShouldWearBodyPiece(uint32_t) const noexcept { return false; }
void ReplayBridge::ActorResetInventory(uint32_t, bool) noexcept {}
void ReplayBridge::ActorEquipSpell(uint32_t, uint32_t, uint32_t) noexcept {}
void ReplayBridge::ActorUnequipSpell(uint32_t, uint32_t, uint32_t) noexcept {}
void ReplayBridge::ActorEquipShout(uint32_t, uint32_t) noexcept {}
void ReplayBridge::ActorUnequipShout(uint32_t, uint32_t) noexcept {}
void ReplayBridge::ActorEquipItem(uint32_t, uint32_t, uint32_t, int32_t) noexcept {}
void ReplayBridge::ActorUnequipItem(uint32_t, uint32_t, uint32_t, int32_t) noexcept {}
// --- IFormBridge extended (Plan 03) ---
bool ReplayBridge::IsBoundWeaponSpell(uint32_t) const noexcept { return false; }
uint32_t ReplayBridge::GetSpellCastingType(uint32_t) const noexcept { return 0; }
bool ReplayBridge::IsSpellEffectWerewolf(uint32_t, uint32_t) const noexcept { return false; }
bool ReplayBridge::IsSpellEffectVampireLord(uint32_t, uint32_t) const noexcept { return false; }
bool ReplayBridge::IsSpellEffectSlow(uint32_t, uint32_t) const noexcept { return false; }
void ReplayBridge::AddMagicTargetEffect(uint32_t, uint32_t, uint32_t, uint32_t, float, bool, bool, bool) noexcept {}

// ---------------------------------------------------------------------------
// IPlayerBridge — stub returns
// ---------------------------------------------------------------------------

uint32_t ReplayBridge::GetPlayerFormId() const noexcept
{
    auto r = NextResponse("GetPlayerFormId");
    if (r.contains("result")) return r["result"].get<uint32_t>();
    return 0x14; // Skyrim player form ID
}
uint32_t ReplayBridge::GetPlayerBaseNpcFormId() const noexcept { return 0; }
void ReplayBridge::GetPlayerPosition(float& aX, float& aY, float& aZ) const noexcept
    { aX = 0.f; aY = 0.f; aZ = 0.f; }
uint32_t ReplayBridge::GetPlayerCellId() const noexcept { return 0; }
uint32_t ReplayBridge::GetPlayerParentCellId() const noexcept { return 0; }
uint32_t ReplayBridge::GetPlayerWorldSpaceId() const noexcept { return 0; }
void ReplayBridge::SetPlayerWaypoint(float, float, float, uint32_t) noexcept {}
void ReplayBridge::RemovePlayerWaypoint() noexcept {}
void ReplayBridge::MovePlayerTo(uint32_t, float, float, float) noexcept {}
std::string ReplayBridge::GetPlayerLocationName() const noexcept { return {}; }
std::string ReplayBridge::GetPlayerWorldSpaceName() const noexcept { return {}; }
bool ReplayBridge::IsPlayerAlive() const noexcept { return true; }
bool ReplayBridge::IsPlayerBleedingOut() const noexcept { return false; }
bool ReplayBridge::IsPlayerInGodMode() const noexcept { return false; }
void ReplayBridge::SetPlayerGodMode(bool) noexcept {}
void ReplayBridge::SetPlayerRespawnMode(bool) noexcept {}
void ReplayBridge::RespawnPlayer() noexcept {}
void ReplayBridge::KnockdownPlayer(float) noexcept {}
void ReplayBridge::FadeScreen(bool, bool, float, bool, float) noexcept {}
void ReplayBridge::PlayerPayGold(uint32_t) noexcept {}
void ReplayBridge::PlayerPayCrimeGoldToAllFactions() noexcept {}
uint32_t ReplayBridge::GetDifficulty() const noexcept { return 3; } // Adept
void ReplayBridge::SetDifficulty(uint32_t) noexcept {}
float ReplayBridge::GetGreetDistance() const noexcept { return 150.f; }
void ReplayBridge::SetGreetDistance(float) noexcept {}
uint16_t ReplayBridge::GetPlayerLevel() const noexcept { return 1; }
float ReplayBridge::GetPlayerActorValue(int32_t) const noexcept { return 0.f; }
void ReplayBridge::ForcePlayerActorValue(int32_t, int32_t, float) noexcept {}
uint32_t ReplayBridge::GetPlayerActiveMagicItemFormId(uint32_t) const noexcept { return 0; }
uint32_t ReplayBridge::GetPlayerEquippedShoutFormId() const noexcept { return 0; }
void ReplayBridge::EquipSpell(uint32_t, uint32_t) noexcept {}
void ReplayBridge::EquipShout(uint32_t) noexcept {}
uint32_t ReplayBridge::GetPlayerRaceFormId() const noexcept { return 0; }
std::vector<uint32_t> ReplayBridge::GetPlayerActiveQuestFormIds() const noexcept { return {}; }
bool ReplayBridge::IsPlayerLoaded() const noexcept { return true; }
void ReplayBridge::ShowHudMessage(const std::string&) noexcept {}
// --- IPlayerBridge extended (Plan 03) ---
QuestLog ReplayBridge::GetPlayerQuestLog() const noexcept { return {}; }
Tints ReplayBridge::GetPlayerFaceTints() const noexcept { return {}; }
void ReplayBridge::AddPlayerSkillExperience(int32_t, float) noexcept {}
int32_t ReplayBridge::GetPlayerLastUsedCombatSkill() const noexcept { return 0; }
uint32_t ReplayBridge::GetPlayerParentCellFormId() const noexcept { return 0; }
bool ReplayBridge::IsActorBleedingOut(uint32_t) const noexcept { return false; }

// ---------------------------------------------------------------------------
// IEventBridge — callbacks stored locally, injected by test code
// ---------------------------------------------------------------------------

void ReplayBridge::RegisterQuestStartStopSink(std::function<void(uint32_t, bool)> aCallback) noexcept
{
    m_questStartStopCb = std::move(aCallback);
}

void ReplayBridge::RegisterQuestStageSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept
{
    m_questStageCb = std::move(aCallback);
}

void ReplayBridge::RegisterActivateSink(std::function<void(uint32_t, uint32_t)> aCallback) noexcept
{
    m_activateCb = std::move(aCallback);
}

void ReplayBridge::RegisterLockChangeSink(std::function<void(uint32_t, bool)> aCallback) noexcept
{
    m_lockChangeCb = std::move(aCallback);
}

void ReplayBridge::UnregisterAllSinks() noexcept
{
    m_questStartStopCb = nullptr;
    m_questStageCb = nullptr;
    m_activateCb = nullptr;
    m_lockChangeCb = nullptr;
}

void ReplayBridge::InjectQuestStartStopEvent(uint32_t aQuestFormId, bool aStarted) noexcept
{
    if (m_questStartStopCb)
        m_questStartStopCb(aQuestFormId, aStarted);
}

void ReplayBridge::InjectQuestStageEvent(uint32_t aQuestFormId, uint32_t aNewStage) noexcept
{
    if (m_questStageCb)
        m_questStageCb(aQuestFormId, aNewStage);
}

void ReplayBridge::InjectActivateEvent(uint32_t aActivatorFormId, uint32_t aActivatedFormId) noexcept
{
    if (m_activateCb)
        m_activateCb(aActivatorFormId, aActivatedFormId);
}

// ---------------------------------------------------------------------------
// IWorldBridge — stub returns
// ---------------------------------------------------------------------------

uint32_t ReplayBridge::GetCurrentWeatherId() const noexcept
{
    auto r = NextResponse("GetCurrentWeatherId");
    if (r.contains("result")) return r["result"].get<uint32_t>();
    return 0;
}
uint32_t ReplayBridge::GetLastWeatherId() const noexcept { return 0; }
void ReplayBridge::ForceWeather(uint32_t, bool) noexcept {}
void ReplayBridge::ReleaseWeatherOverride() noexcept {}
std::string ReplayBridge::GetModName(uint32_t) const noexcept { return {}; }
void ReplayBridge::SetGameWeatherSystemEnabled(bool) noexcept {}
float ReplayBridge::GetGameTimeHours() const noexcept { return 12.f; }
void ReplayBridge::SetGameTimeHours(float) noexcept {}
float ReplayBridge::GetGameTimeScale() const noexcept { return 20.f; }
float ReplayBridge::GetGameYear() const noexcept { return 201.f; }
float ReplayBridge::GetGameMonth() const noexcept { return 1.f; }
float ReplayBridge::GetGameDay() const noexcept { return 1.f; }
