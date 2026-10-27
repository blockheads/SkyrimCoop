#include "HudMessages.h"

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <deque>
#include <mutex>

namespace TestHarness::HudMessages
{
namespace
{
// Scripts show them from the VM's threads.
std::mutex s_lock;
std::deque<std::string> s_recent;
constexpr size_t kKept = 32;

// SendHUDMessage::ShowHUDMessage (52933): the text, a sound, and whether to queue it (Utils::ShowHudMessage).
using TShowHudMessage = void(const char* acpText, const char* acpSound, bool aQueue);
TShowHudMessage* RealShowHudMessage = nullptr;

void HookShowHudMessage(const char* acpText, const char* acpSound, bool aQueue)
{
    {
        std::scoped_lock _{s_lock};
        s_recent.emplace_back(acpText ? acpText : "");
        if (s_recent.size() > kKept)
            s_recent.pop_front();
    }
    RealShowHudMessage(acpText, acpSound, aQueue);
}

Hooks::Initializer s_hudMessageHooks([]() { HOOK_ID(52933, &RealShowHudMessage, HookShowHudMessage); });
} // namespace

std::vector<std::string> Recent()
{
    std::scoped_lock _{s_lock};
    return {s_recent.begin(), s_recent.end()};
}
} // namespace TestHarness::HudMessages
