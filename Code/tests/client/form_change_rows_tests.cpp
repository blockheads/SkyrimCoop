// Which recorded form changes the host sends its guests as rows (Games/FormChangeRows.h).
#include <catch2/catch.hpp>

#include <Games/FormChangeRows.h>

using FormChangeRows::Form;

TEST_CASE("A reference's states and a cell's or an actor's owner become rows; an actor's others do not", "[FormChangeRows]")
{
    constexpr uint32_t cReferenceFlags = 0x00FF0000;
    CHECK(FormChangeRows::Sent(Form::kReference, 0x00110001, cReferenceFlags) == 0x00110000);
    CHECK(FormChangeRows::Sent(Form::kCell, FormChangeRows::cCellOwnership | 0x2, cReferenceFlags) == FormChangeRows::cCellOwnership);
    // An actor's states are its owner's game's; who owns it (a horse bought) is the world's.
    CHECK(FormChangeRows::Sent(Form::kActor, 0xFFFFFFFF, cReferenceFlags) == FormChangeRows::cReferenceOwnership);
    CHECK(FormChangeRows::Sent(Form::kActor, 0x00FF0002, cReferenceFlags) == 0);
    CHECK(FormChangeRows::Sent(Form::kOther, 0xFFFFFFFF, cReferenceFlags) == 0);
}
