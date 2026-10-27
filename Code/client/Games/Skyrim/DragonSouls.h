#pragma once

// DragonSouls' value sink (38505): a gain shows "Dragon soul absorbed" and sends DragonSoulsGained. While a
// ScopedSilence lives, this thread's writes show and send nothing: a joining character's own count is set, not gained
// (docs/design/dragonborn.md, "Joining and rejoining").
namespace DragonSouls
{
class ScopedSilence
{
  public:
    ScopedSilence() noexcept;
    ~ScopedSilence() noexcept;
    ScopedSilence(const ScopedSilence&) = delete;
    ScopedSilence& operator=(const ScopedSilence&) = delete;

  private:
    bool m_outer;
};
} // namespace DragonSouls
