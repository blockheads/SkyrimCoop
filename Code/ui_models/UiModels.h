#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>
#include <unordered_map>

namespace Rml
{
class Context;
}

namespace Ui
{
// One registry per UI session, shared by party rows and chat history.
class PlayerColors
{
  public:
    Rml::String ForName(const Rml::String& aName);

  private:
    std::unordered_map<Rml::String, Rml::String> m_colors;
};

struct Member
{
    Rml::String Name;
    bool IsHost = false;
    int Level = 0;
    // 0..1 of health; -1 while the character is absent.
    float Health = -1.f;
    Rml::String Color = "#64b5ff";

    Rml::String Face();
    bool operator==(const Member&) const = default;
};

struct ChatLine
{
    Rml::String Name;
    Rml::String Text;
    bool System = false;
    Rml::String Color = "#c0c0c0";
    // A player went down: the whole line in red, no speaker.
    bool Perished = false;

    bool operator==(const ChatLine&) const = default;
};

struct PartyModel
{
    Rml::Vector<Member> Members;
    bool Visible = true; // Fixtures show the panel unless explicitly hidden; the game drives activity.
    float Opacity = 1.f;
    Rml::DataModelHandle Handle;
    bool Bind(Rml::Context& aContext);
};

struct ChatModel
{
    Rml::Vector<ChatLine> Lines;
    Rml::String Draft;
    bool Open = false;
    Rml::DataModelHandle Handle;
    bool Bind(Rml::Context& aContext);
};

// Presentation data only; the session owns transitions and refusal wording.
struct JoinModel
{
    bool Visible = false;
    Rml::String State = "idle";
    int Step = 0;
    int Completed = 0;
    float Progress = -1.f;
    Rml::String Address;
    Rml::String Reason;
    Rml::String Message;
    Rml::Vector<Rml::String> Details;
    Rml::DataModelHandle Handle;
    bool Bind(Rml::Context& aContext);
    // What Copy puts on the clipboard: the state, the stable reason, the sentence and each detail, a line each.
    [[nodiscard]] Rml::String CopyText() const;
};
} // namespace Ui
