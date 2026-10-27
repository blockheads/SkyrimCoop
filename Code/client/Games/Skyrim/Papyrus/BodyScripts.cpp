#include <Papyrus/BodyScripts.h>

#include <Forms/TESForm.h>
#include <Papyrus/OwnEvents.h>
#include <RTTI.h>
#include <TESObjectREFR.h>

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace BodyScripts
{
namespace
{
std::atomic<bool> s_inSession{false};
// The quests a body's script started here. Its lock is a leaf.
std::mutex s_lock;
std::unordered_set<uint32_t> s_bodyQuests;

// An active effect's handle (OwnEvents: 0x0002'<unique id>'<its actor>).
constexpr bool IsEffect(uint64_t aScript) noexcept
{
    return static_cast<uint16_t>(aScript >> 48) == 2;
}
} // namespace

void SetInSession(bool aInSession) noexcept
{
    s_inSession = aInSession;
    std::scoped_lock _{s_lock};
    s_bodyQuests.clear();
}

void QuestStarted(uint32_t aQuest, uint64_t aScript) noexcept
{
    if (!s_inSession.load(std::memory_order_relaxed))
        return;
    const bool cBody = IsBody(aScript);
    std::scoped_lock _{s_lock};
    if (cBody)
        s_bodyQuests.insert(aQuest);
    else
        s_bodyQuests.erase(aQuest);
}

bool IsBody(uint64_t aScript) noexcept
{
    if (!aScript)
        return false;
    if (IsEffect(aScript) || OwnEvents::IsAboutPlayer(aScript))
        return true;
    if (const uint32_t cQuest = OwnEvents::QuestOf(aScript))
    {
        std::scoped_lock _{s_lock};
        if (s_bodyQuests.contains(cQuest))
            return true;
    }
    // Equipment: a weapon's or an armor's script, on its reference or its base.
    const TESForm* pForm = TESForm::GetById(static_cast<uint32_t>(aScript));
    if (const auto* pReference = Cast<TESObjectREFR>(const_cast<TESForm*>(pForm)))
        pForm = pReference->baseForm;
    return pForm && (pForm->formType == FormType::Armor || pForm->formType == FormType::Weapon);
}
} // namespace BodyScripts
