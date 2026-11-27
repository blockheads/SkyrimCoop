#include <Structs/ActorStatePart.h>

bool ActorStatePart::SameState(const ActorStatePart& acRhs) const noexcept
{
    return Flags1 == acRhs.Flags1 && Flags2 == acRhs.Flags2 && InKillMove == acRhs.InKillMove && Killer == acRhs.Killer && HandsUp == acRhs.HandsUp &&
           Noise == acRhs.Noise;
}

