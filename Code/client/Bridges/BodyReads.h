#pragma once

#include <Bridges/IBodyReads.h>

// This game's physics as PhysicsBodies reads it: the change funnel's havok moves (watched while it lives) and the
// engine's bodies and nodes. One at a time: the funnel's moved set is shared.
class BodyReads final : public IBodyReads
{
public:
    BodyReads() noexcept;
    ~BodyReads() noexcept override;

    BodyReads(const BodyReads&) = delete;
    BodyReads& operator=(const BodyReads&) = delete;

    [[nodiscard]] std::vector<uint32_t> TakeMovedReferences() noexcept override;
    [[nodiscard]] uint32_t PlayerInRagdoll() noexcept override;
    [[nodiscard]] std::optional<Reference> ReadReference(uint32_t aFormId, bool aBodies = true) noexcept override;
    void WriteNode(uint32_t aFormId, uint8_t aIndex) noexcept override;
};
