#pragma once

namespace SpeakerUpdate
{
// Actor::UpdateInDialogue (37541) reads the speaker's process unchecked, and a placed actor marked deleted keeps its
// handle with none (docs/re/systems/copy-lifecycle.md): skipped in a session, which then ends the conversation.
[[nodiscard]] constexpr bool Skipped(bool aHasProcess, bool aOnline) noexcept
{
    return !aHasProcess && aOnline;
}
} // namespace SpeakerUpdate
