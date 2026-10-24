#include "PresentationChecks.h"
#include <ui_models/HudVisibility.h>
#include <ui_models/PartyActivity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_set>

bool CheckPresentationPolicy()
{
    try
    {
        const auto require = [](bool condition, const char* message) {
            if (!condition)
                throw std::runtime_error(message);
        };
        Ui::HudVisibility hud;
        require(!hud.AllowsParty(), "An absent native HUD allowed the party panel");
        hud.MenuSystemVisible = hud.HudMenuOpen = hud.MovieReady = true;
        require(hud.AllowsParty(), "A replacement HUD did not use engine visibility");
        hud.HealthClipVisible = false;
        require(!hud.AllowsParty(), "The native health HUD mode did not hide the party panel");
        hud.HealthClipVisible = true;
        hud.BlockingMenu = true;
        require(!hud.AllowsParty(), "A blocking menu allowed the party panel");

        Ui::PlayerColors colors;
        std::unordered_set<Rml::String> assigned;
        for (int i = 0; i < 40; ++i)
        {
            const Rml::String name = "Player " + std::to_string(i);
            const Rml::String color = colors.ForName(name);
            require(assigned.insert(color).second, "Different players received the same name color");
            require(colors.ForName(name) == color, "A player's name color changed");
        }
        const Rml::String firstColor = colors.ForName("Player 0");
        colors.ForName("A late arrival");
        require(colors.ForName("Player 0") == firstColor, "A joining player changed an existing color");

        Ui::PartyActivity activity;
        Rml::Vector<Ui::Member> members;
        const auto update = [&](double seconds, bool hud = true, bool connected = true) {
            const auto time =
                Ui::PartyActivity::Clock::time_point{} +
                std::chrono::duration_cast<Ui::PartyActivity::Clock::duration>(std::chrono::duration<double>(seconds));
            activity.Update(members, connected, hud, time);
        };
        update(0);
        require(activity.Opacity() == 0.f, "An empty party should start hidden");
        members = {{"Lydia", true, 32, 1.f}};
        update(1);
        require(activity.Opacity() == 1.f, "Joining did not show the panel");
        update(9.2);
        require(std::abs(activity.Opacity() - 0.5f) < 0.01f, "The panel did not fade after eight seconds");
        update(9.5);
        require(activity.Opacity() == 0.f, "An idle party stayed visible");
        members[0].Level = 33;
        members[0].Color = "#ffffff";
        update(10);
        require(activity.Opacity() == 0.f, "A presentation/level update restarted the health timer");
        members[0].Health = 0.9996f;
        update(11);
        require(activity.Opacity() == 0.f, "Insignificant health jitter showed the panel");
        members[0].Health = 0.9988f;
        update(12);
        require(activity.Opacity() == 1.f, "Small cumulative health changes were missed");
        members[0].Health = 0.0001f;
        update(30);
        update(39);
        require(activity.Opacity() == 0.f, "Unchanging low health kept the panel visible");
        members[0].Health = 0.f;
        update(40);
        require(activity.Opacity() == 1.f, "Death below the health-change threshold was missed");
        update(49);
        members[0].Health = 0.0001f;
        update(50);
        require(activity.Opacity() == 1.f, "Revival below the health-change threshold was missed");
        update(59);
        members[0].Health = -1.f;
        update(60);
        require(activity.Opacity() == 1.f, "An unavailable character transition was missed");
        members[0].Health = 1.f;
        update(70);
        require(activity.Opacity() == 1.f, "A character returning from loading was missed");
        members.push_back({"Serana", false, 28, 1.f});
        update(80);
        update(89);
        std::reverse(members.begin(), members.end());
        update(90);
        require(activity.Opacity() == 0.f, "Reordering the party restarted the timer");
        members.erase(members.begin());
        update(91);
        require(activity.Opacity() == 1.f, "A departing player was missed");
        members.clear();
        update(100);
        require(activity.Opacity() == 1.f, "The final departure was missed");
        update(109);
        require(activity.Opacity() == 0.f, "The final departure stayed visible");

        members = {{"Lydia", true, 32, 1.f}};
        update(110, false);
        update(140, false);
        require(activity.Opacity() == 0.f, "The panel appeared over a hidden native HUD");
        update(150);
        require(activity.Opacity() == 1.f, "Activity during a menu expired before the HUD returned");
        update(151, false);
        update(180);
        require(activity.Opacity() == 1.f, "A menu interrupted and lost a visible notification");
        update(189);
        update(190, false);
        update(210);
        require(activity.Opacity() == 0.f, "Opening a menu alone woke an inactive panel");
        members[0].Health = 0.5f;
        update(220, false);
        update(230, false, false);
        members.clear();
        update(240);
        require(activity.Opacity() == 0.f, "Disconnect retained stale party activity");
        std::printf("checked distinct/stable player colors and party activity, fade, HUD suppression and disconnect\n");
        return true;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Presentation check failed: %s\n", error.what());
        return false;
    }
}
