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

TEST_CASE("A script's placed hazard is a reference: its later rows (disabled, deleted) are sent", "[FormChangeRows]")
{
    CHECK(FormChangeRows::OfFormType(61) == Form::kReference);
    CHECK(FormChangeRows::OfFormType(70) == Form::kReference);
    CHECK(FormChangeRows::OfFormType(62) == Form::kActor);
    CHECK(FormChangeRows::OfFormType(60) == Form::kCell);
    CHECK(FormChangeRows::OfFormType(43) == Form::kOther);
}
