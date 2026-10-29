#include <Services/PartyOptionText.h>

#include <algorithm>

std::string PartyOptionText(const std::string& acText, const std::vector<std::string>& acNames)
{
    if (acNames.empty())
        return acText;
    std::string text = acText + " [";
    for (size_t i = 0; i < acNames.size(); ++i)
        text += (i ? ", " : "") + acNames[i];
    return text + "]";
}

std::string CannotSayText(const std::string& acPlayerName)
{
    return "I'm sorry " + acPlayerName + ", you don't know how to say that.";
}

bool CanSay(const std::vector<uint32_t>& acPlayers, uint32_t aPlayerId)
{
    return acPlayers.empty() || std::ranges::find(acPlayers, aPlayerId) != acPlayers.end();
}
