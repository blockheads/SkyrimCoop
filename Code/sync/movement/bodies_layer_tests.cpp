#include <movement/BodiesLayer.h>

#include <catch2/catch.hpp>

using BodiesLayer::For;
using BodiesLayer::Op;
using BodiesLayer::Steps;

TEST_CASE("A copy's bodies handed back to its owner's layer 0 leave the world keyframed", "[copy-ragdoll][bodies-layer]")
{
    // A victim half cut short: its ragdoll in the world (layer 8), its limbs dynamic.
    CHECK(For(8, 0, true, true, true) == Steps{Op::kRemove, Op::kKeyframe, Op::kNone});
    // Out already, its root keyframed but a limb still dynamic: still to do.
    CHECK(For(0, 0, true, true, false) == Steps{Op::kRemove, Op::kKeyframe, Op::kNone});
    // Out and keyframed: nothing.
    CHECK(For(0, 0, true, false, false) == Steps{});
}

TEST_CASE("A copy's bodies put on its owner's furniture layer are keyframed first", "[copy-ragdoll][bodies-layer]")
{
    CHECK(For(0, 33, true, false, false) == Steps{Op::kKeyframe, Op::kSetLayer, Op::kAddToWorld});
    CHECK(For(8, 33, true, true, true) == Steps{Op::kKeyframe, Op::kSetLayer, Op::kNone});
    CHECK(For(33, 33, false, false, true) == Steps{Op::kKeyframe, Op::kSetLayer, Op::kNone}); // a mount's group
    CHECK(For(33, 33, true, false, true) == Steps{});
}
