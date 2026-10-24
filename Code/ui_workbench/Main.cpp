#include "Fixtures.h"
#include "HeadlessDisplay.h"
#include "PresentationChecks.h"
#include "WorkbenchRenderer.h"

#include <RmlUi/Core.h>
#include <RmlUi_Include_GL3.h>
#include <RmlUi_Platform_GLFW.h>
#include <RmlUi_Renderer_GL3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;

struct System final : SystemInterface_GLFW
{
    bool Frozen = false;
    int Errors = 0;
    double GetElapsedTime() override
    {
        return Frozen ? 1.0 : SystemInterface_GLFW::GetElapsedTime();
    }
    bool LogMessage(Rml::Log::Type aType, const Rml::String& acMessage) override
    {
        if (aType <= Rml::Log::LT_WARNING)
        {
            ++Errors;
            std::fprintf(stderr, "RmlUi: %s\n", acMessage.c_str());
        }
        return true;
    }
};

struct Options
{
    bool Check = false;
    Rml::String Fixture = "join-idle";
    Rml::String Output;
    int Width = 1920;
    int Height = 1080;
};

bool Arguments(int aCount, char** apArguments, Options& aOptions)
{
    for (int i = 1; i < aCount; ++i)
    {
        const std::string_view arg(apArguments[i]);
        if (arg == "--check")
            aOptions.Check = true;
        else if (arg == "--shot" && i + 2 < aCount)
        {
            aOptions.Fixture = apArguments[++i];
            aOptions.Output = apArguments[++i];
        }
        else if (arg == "--fixture" && i + 1 < aCount)
            aOptions.Fixture = apArguments[++i];
        else if (arg == "--size" && i + 1 < aCount)
        {
            char trailing;
            if (std::sscanf(apArguments[++i], "%dx%d%c", &aOptions.Width, &aOptions.Height, &trailing) != 2 ||
                aOptions.Width < 320 || aOptions.Width > 7680 || aOptions.Height < 240 || aOptions.Height > 4320)
                return false;
        }
        else
            return false;
    }
    return !aOptions.Check || aOptions.Output.empty();
}

fs::path Assets()
{
    if (const char* pDirectory = std::getenv("SKYRIMCOOP_UI_DIR"); pDirectory && *pDirectory)
        return pDirectory;
    return UI_ASSET_DIRECTORY;
}

// Include paths and times so additions and deletions also trigger reloads.
std::vector<std::pair<fs::path, fs::file_time_type>> Changes(const fs::path& acDirectory)
{
    std::vector<std::pair<fs::path, fs::file_time_type>> result;
    std::error_code error;
    for (fs::recursive_directory_iterator it(acDirectory, error), end; !error && it != end; it.increment(error))
        if (it->is_regular_file(error))
            result.emplace_back(it->path(), it->last_write_time(error));
    std::sort(result.begin(), result.end());
    return result;
}

struct Workbench final : Rml::EventListener
{
    GLFWwindow* Window = nullptr;
    Rml::Context* Context = nullptr;
    fs::path Directory;
    Ui::PartyModel Party;
    Ui::ChatModel Chat;
    Ui::JoinModel Join;
    Ui::PlayerColors PlayerColors;
    Rml::ElementDocument* JoinDocument = nullptr;
    std::vector<fs::path> Fixtures;
    fs::path Fixture;
    bool Next = false;
    bool Previous = false;
    bool Reload = false;
    int Size = 0;

    bool Load()
    {
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();
        JoinDocument = nullptr;
        for (const char* pName : {"party.rml", "chat.rml", "join.rml"})
        {
            auto* pDocument = Context->LoadDocument((Directory / pName).string());
            if (!pDocument)
                return false;
            pDocument->Show();
            if (std::string_view(pName) == "join.rml")
            {
                JoinDocument = pDocument;
                pDocument->AddEventListener(Rml::EventId::Click, this);
            }
        }
        return true;
    }

    bool Select(const fs::path& acFixture)
    {
        if (!LoadFixture(acFixture, Party, Chat, Join, PlayerColors))
            return false;
        Fixture = acFixture;
        const auto title = "UiWorkbench | " + Fixture.stem().string() + " | F6/F7 fixture, F5 reload, F9 size";
        glfwSetWindowTitle(Window, title.c_str());
        return true;
    }

    void ProcessEvent(Rml::Event& aEvent) override
    {
        auto* pTarget = aEvent.GetTargetElement();
        while (pTarget && pTarget->GetId().empty())
            pTarget = pTarget->GetParentNode();
        if (!pTarget)
            return;
        const auto& id = pTarget->GetId();
        if (id == "copy")
        {
            glfwSetClipboardString(Window, Join.CopyText().c_str());
        }
        else if (id == "back")
        {
            const auto handle = Join.Handle;
            Join = {};
            Join.Handle = handle;
            Join.Visible = true;
        }
        Join.Handle.DirtyAllVariables();
    }
};

Workbench& App(GLFWwindow* apWindow)
{
    return *static_cast<Workbench*>(glfwGetWindowUserPointer(apWindow));
}

int Modifiers(GLFWwindow* apWindow)
{
    int mods = 0;
    for (const auto [key, mask] : {std::pair{GLFW_KEY_LEFT_SHIFT, GLFW_MOD_SHIFT},
                                   {GLFW_KEY_RIGHT_SHIFT, GLFW_MOD_SHIFT},
                                   {GLFW_KEY_LEFT_CONTROL, GLFW_MOD_CONTROL},
                                   {GLFW_KEY_RIGHT_CONTROL, GLFW_MOD_CONTROL},
                                   {GLFW_KEY_LEFT_ALT, GLFW_MOD_ALT},
                                   {GLFW_KEY_RIGHT_ALT, GLFW_MOD_ALT},
                                   {GLFW_KEY_LEFT_SUPER, GLFW_MOD_SUPER},
                                   {GLFW_KEY_RIGHT_SUPER, GLFW_MOD_SUPER}})
        if (glfwGetKey(apWindow, key) == GLFW_PRESS)
            mods |= mask;
    return mods;
}

void Input(Workbench& aApp)
{
    auto* pWindow = aApp.Window;
    glfwSetWindowUserPointer(pWindow, &aApp);
    glfwSetKeyCallback(pWindow, [](GLFWwindow* pWindow, int key, int, int action, int mods) {
        auto& app = App(pWindow);
        if (key == GLFW_KEY_F5 || key == GLFW_KEY_F6 || key == GLFW_KEY_F7 || key == GLFW_KEY_F9)
        {
            if (action == GLFW_PRESS)
            {
                if (key == GLFW_KEY_F5)
                    app.Reload = true;
                if (key == GLFW_KEY_F6)
                    app.Next = true;
                if (key == GLFW_KEY_F7)
                    app.Previous = true;
                if (key == GLFW_KEY_F9)
                {
                    constexpr std::array sizes{std::pair{1920, 1080}, std::pair{1280, 720}, std::pair{2560, 1440}};
                    const auto [width, height] = sizes[++app.Size % sizes.size()];
                    glfwSetWindowSize(pWindow, width, height);
                }
            }
            return;
        }
        RmlGLFW::ProcessKeyCallback(app.Context, key, action, mods);
    });
    glfwSetCharCallback(pWindow, [](GLFWwindow* pWindow, unsigned int codepoint) {
        RmlGLFW::ProcessCharCallback(App(pWindow).Context, codepoint);
    });
    glfwSetCursorEnterCallback(pWindow, [](GLFWwindow* pWindow, int entered) {
        RmlGLFW::ProcessCursorEnterCallback(App(pWindow).Context, entered);
    });
    glfwSetCursorPosCallback(pWindow, [](GLFWwindow* pWindow, double x, double y) {
        RmlGLFW::ProcessCursorPosCallback(App(pWindow).Context, pWindow, x, y, Modifiers(pWindow));
    });
    glfwSetMouseButtonCallback(pWindow, [](GLFWwindow* pWindow, int button, int action, int mods) {
        RmlGLFW::ProcessMouseButtonCallback(App(pWindow).Context, button, action, mods);
    });
    glfwSetScrollCallback(pWindow, [](GLFWwindow* pWindow, double, double y) {
        RmlGLFW::ProcessScrollCallback(App(pWindow).Context, y, Modifiers(pWindow));
    });
    glfwSetWindowFocusCallback(pWindow, [](GLFWwindow* pWindow, int focused) {
        if (!focused)
            App(pWindow).Context->ProcessMouseLeave();
    });
}

bool CheckInput(Workbench& aApp)
{
    auto select = [&](const char* apName) {
        aApp.Context->UnloadAllDocuments();
        aApp.Context->Update();
        if (!aApp.Select(aApp.Directory / "fixtures" / apName) || !aApp.Load())
            return false;
        aApp.Context->Update();
        aApp.Context->Update();
        return true;
    };
    auto click = [&](const char* apId) {
        auto* pElement = aApp.JoinDocument->GetElementById(apId);
        if (!pElement)
            return false;
        const auto position =
            pElement->GetAbsoluteOffset(Rml::BoxArea::Border) + pElement->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
        aApp.Context->ProcessMouseMove(int(position.x), int(position.y), 0);
        aApp.Context->ProcessMouseButtonDown(0, 0);
        aApp.Context->ProcessMouseButtonUp(0, 0);
        aApp.Context->Update();
        return true;
    };
    if (!select("join-refused-mods_differ.json") || !click("copy"))
        return false;
    const char* pClipboard = glfwGetClipboardString(aApp.Window);
    if (!pClipboard || std::string_view(pClipboard).find("reason mods_differ") == std::string_view::npos ||
        std::string_view(pClipboard).find("detail missing ") == std::string_view::npos || !click("back") ||
        aApp.Join.State != "idle")
        return false;
    aApp.Context->UnloadAllDocuments();
    aApp.Context->Update();
    if (!aApp.Load())
        return false;
    aApp.Context->Update();
    aApp.Context->Update();
    return aApp.JoinDocument->GetElementById("address") == nullptr &&
           aApp.JoinDocument->GetElementById("host-action") == nullptr;
}

bool CheckMoods(Workbench& aApp)
{
    aApp.Context->UnloadAllDocuments();
    aApp.Context->Update();
    aApp.Party.Members = {{"Lydia", true, 32, 1.f}};
    aApp.Party.Handle.DirtyVariable("members");
    if (!aApp.Load())
        return false;
    for (const auto& [health, icon] : {std::pair{-1.f, "neutral"},
                                       {0.f, "dead"},
                                       {0.001f, "very-tired"},
                                       {0.15f, "tired"},
                                       {0.30f, "sad"},
                                       {0.45f, "neutral"},
                                       {0.60f, "happy"},
                                       {0.75f, "really-happy"},
                                       {0.90f, "very-happy"},
                                       {1.f, "very-happy"}})
    {
        aApp.Party.Members[0].Health = health;
        aApp.Party.Members[0].Level = health < 0.f ? 0 : 32;
        aApp.Party.Handle.DirtyVariable("members");
        aApp.Context->Update();
        aApp.Context->Update();
        auto* pImage = aApp.Context->GetDocument(0)->QuerySelector("img");
        if (health < 0.f)
        {
            auto* pStatus = aApp.Context->GetDocument(0)->QuerySelector(".status");
            if (!pStatus || !pStatus->IsVisible(true) || pStatus->GetInnerRML() != "Loading")
                return false;
        }
        if (!pImage || pImage->GetAttribute<Rml::String>("src", "") != "moods/" + Rml::String(icon) + ".png")
        {
            std::fprintf(stderr, "Mood binding check failed at health %.3f\n", health);
            return false;
        }
    }
    std::printf("checked health face changes, thresholds, death and unavailable health\n");
    return true;
}

bool CheckPartyBindings(Workbench& aApp)
{
    aApp.Context->UnloadAllDocuments();
    aApp.Context->Update();
    if (!aApp.Select(aApp.Directory / "fixtures/chat-open.json") || !aApp.Load())
        return false;
    const auto update = [&] {
        aApp.Context->Update();
        aApp.Context->Update();
    };
    update();
    const auto checkColors = [&] {
        Rml::ElementList partyNames, chatNames;
        aApp.Context->GetDocument(0)->QuerySelectorAll(partyNames, ".name");
        aApp.Context->GetDocument(1)->QuerySelectorAll(chatNames, ".line .name");
        if (partyNames.size() != aApp.Party.Members.size() || chatNames.size() != aApp.Chat.Lines.size())
            return false;
        for (size_t i = 0; i < partyNames.size(); ++i)
        {
            const auto color = partyNames[i]->GetProperty<Rml::Colourb>("color");
            for (size_t j = i + 1; j < partyNames.size(); ++j)
                if (aApp.Party.Members[i].Name != aApp.Party.Members[j].Name &&
                    color == partyNames[j]->GetProperty<Rml::Colourb>("color"))
                    return false;
            for (size_t j = 0; j < chatNames.size(); ++j)
                if (!aApp.Chat.Lines[j].System && aApp.Chat.Lines[j].Name == aApp.Party.Members[i].Name &&
                    color != chatNames[j]->GetProperty<Rml::Colourb>("color"))
                    return false;
        }
        return chatNames.front()->GetProperty<Rml::Colourb>("color") == Rml::Colourb(192, 192, 192);
    };
    if (!checkColors())
        return false;
    std::reverse(aApp.Party.Members.begin(), aApp.Party.Members.end());
    aApp.Party.Handle.DirtyVariable("members");
    update();
    if (!checkColors())
        return false;
    aApp.Party.Members.push_back({"Mjoll", false, 35, 1.f, aApp.PlayerColors.ForName("Mjoll")});
    aApp.Chat.Lines.push_back({"Mjoll", "On my way.", false, aApp.PlayerColors.ForName("Mjoll")});
    aApp.Party.Handle.DirtyVariable("members");
    aApp.Chat.Handle.DirtyVariable("lines");
    update();
    if (!checkColors())
        return false;
    auto* pParty = aApp.Context->GetDocument(0)->GetElementById("party");
    aApp.Party.Visible = false;
    aApp.Party.Handle.DirtyVariable("visible");
    update();
    if (!pParty || pParty->IsVisible(true))
        return false;
    aApp.Party.Visible = true;
    aApp.Party.Opacity = 0.5f;
    aApp.Party.Handle.DirtyAllVariables();
    update();
    if (!pParty->IsVisible(true) || pParty->GetProperty<float>("opacity") != 0.5f)
        return false;
    std::printf("checked name colors across party/chat, roster updates and hidden/fading panel\n");
    return true;
}

bool Screenshot(const Options& acOptions)
{
    const size_t stride = size_t(acOptions.Width) * 4;
    std::vector<unsigned char> pixels(stride * acOptions.Height);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, acOptions.Width, acOptions.Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < acOptions.Height / 2; ++y)
        std::swap_ranges(pixels.begin() + y * stride, pixels.begin() + (y + 1) * stride,
                         pixels.begin() + (acOptions.Height - y - 1) * stride);
    return stbi_write_png(acOptions.Output.c_str(), acOptions.Width, acOptions.Height, 4, pixels.data(), int(stride)) !=
           0;
}

int Run(GLFWwindow* apWindow, const Options& acOptions)
{
    System system;
    system.Frozen = acOptions.Check || !acOptions.Output.empty();
    system.SetWindow(apWindow);
    WorkbenchRenderer renderer;
    if (!renderer)
        return 1;
    Rml::SetSystemInterface(&system);
    Rml::SetRenderInterface(&renderer);
    if (!Rml::Initialise())
        return 1;

    int result = 0;
    {
        Workbench app;
        app.Window = apWindow;
        app.Directory = Assets();
        app.Context = Rml::CreateContext("workbench", {acOptions.Width, acOptions.Height});
        if (!app.Context || !Rml::LoadFontFace((app.Directory / "fonts/Oswald-Variable.ttf").string()))
        {
            Rml::Shutdown();
            return 1;
        }
        if (!app.Party.Bind(*app.Context) || !app.Chat.Bind(*app.Context) || !app.Join.Bind(*app.Context))
        {
            Rml::Shutdown();
            return 1;
        }
        for (const char* pFont : {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf"})
            if (!Rml::LoadFontFace((app.Directory / "fonts" / pFont).string()))
            {
                Rml::Shutdown();
                return 1;
            }
        Input(app);
        std::error_code error;
        for (fs::directory_iterator it(app.Directory / "fixtures", error), end; !error && it != end;
             it.increment(error))
            if (it->path().extension() == ".json")
                app.Fixtures.push_back(it->path());
        std::sort(app.Fixtures.begin(), app.Fixtures.end());
        if (error || app.Fixtures.empty())
        {
            std::fprintf(stderr, "No fixtures in %s/fixtures\n", app.Directory.c_str());
            Rml::Shutdown();
            return 1;
        }
        fs::path selected(acOptions.Fixture);
        if (!fs::exists(selected))
        {
            selected = app.Directory / "fixtures" / selected;
            if (selected.extension() != ".json")
                selected += ".json";
        }
        auto changes = Changes(app.Directory);
        auto nextCheck = std::chrono::steady_clock::now();
        size_t checkIndex = 0;
        bool running = true;
        while (running && !glfwWindowShouldClose(apWindow))
        {
            if (acOptions.Check)
                selected = app.Fixtures[checkIndex];
            if (app.Fixture.empty() || app.Reload || app.Next || app.Previous || acOptions.Check)
            {
                if (app.Next || app.Previous)
                {
                    const auto it = std::find(app.Fixtures.begin(), app.Fixtures.end(), app.Fixture);
                    const size_t index = it == app.Fixtures.end() ? 0 : size_t(it - app.Fixtures.begin());
                    selected =
                        app.Fixtures[(index + (app.Previous ? app.Fixtures.size() - 1 : 1)) % app.Fixtures.size()];
                }
                app.Context->UnloadAllDocuments();
                app.Context->Update();
                if (!app.Select(selected) || !app.Load())
                {
                    result = 1;
                    break;
                }
                app.Next = app.Previous = app.Reload = false;
            }
            int width, height;
            glfwGetFramebufferSize(apWindow, &width, &height);
            if (width > 0 && height > 0)
            {
                app.Context->SetDimensions({width, height});
                app.Context->SetDensityIndependentPixelRatio(float(height) / 1080.f);
                // Two updates settle data-for and layout before drawing or checking.
                app.Context->Update();
                app.Context->Update();
                renderer.SetViewport(width, height);
                glClearColor(0.055f, 0.060f, 0.065f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT);
                renderer.BeginFrame();
                app.Context->Render();
                renderer.EndFrame();
            }
            if (acOptions.Check)
            {
                std::printf("checked %s (party, chat, join)\n", selected.filename().c_str());
                running = ++checkIndex < app.Fixtures.size();
            }
            else if (!acOptions.Output.empty())
            {
                if (width != acOptions.Width || height != acOptions.Height || !Screenshot(acOptions))
                {
                    std::fprintf(stderr, "Could not write screenshot: %s\n", acOptions.Output.c_str());
                    result = 1;
                }
                running = false;
            }
            else
            {
                glfwSwapBuffers(apWindow);
                glfwWaitEventsTimeout(1.0 / 60.0);
                const auto now = std::chrono::steady_clock::now();
                if (now >= nextCheck)
                {
                    nextCheck = now + std::chrono::seconds(1);
                    auto current = Changes(app.Directory);
                    if (current != changes)
                    {
                        changes = std::move(current);
                        app.Reload = true;
                    }
                }
            }
        }
        if (acOptions.Check && !result)
        {
            if (!CheckInput(app))
            {
                std::fprintf(stderr, "UI input check failed (Copy, Back, Steam invite screen, reload).\n");
                result = 1;
            }
            else
                std::printf("checked mouse, Copy, Back, Steam invite screen and reload\n");
            if (!result && !CheckMoods(app))
                result = 1;
            if (!result && (!CheckPresentationPolicy() || !CheckPartyBindings(app)))
                result = 1;
        }
        app.Context->UnloadAllDocuments();
        app.Context->Update();
        glfwSetWindowUserPointer(apWindow, nullptr);
    }
    Rml::Shutdown();
    return result || system.Errors ? 1 : 0;
}
} // namespace

int main(int aCount, char** apArguments)
{
    Options options;
    if (!Arguments(aCount, apArguments, options))
    {
        std::fprintf(stderr, "UiWorkbench [--fixture name] [--size WxH]\nUiWorkbench --shot fixture out.png [--size "
                             "WxH]\nUiWorkbench --check\n");
        return 2;
    }
    HeadlessDisplay display;
    const bool headless = options.Check || !options.Output.empty();
    if (headless && !display.Start())
    {
        std::fprintf(stderr, "Could not start Xvfb for off-screen rendering. Install Xvfb.\n");
        return 1;
    }
    glfwSetErrorCallback([](int, const char* pMessage) { std::fprintf(stderr, "GLFW: %s\n", pMessage); });
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, headless ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    auto* pWindow = glfwCreateWindow(options.Width, options.Height, "UiWorkbench", nullptr, nullptr);
    if (!pWindow)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(pWindow);
    glfwSwapInterval(headless ? 0 : 1);
    const bool initialized = RmlGL3::Initialize();
    const int result = initialized ? Run(pWindow, options) : 1;
    if (initialized)
        RmlGL3::Shutdown();
    glfwDestroyWindow(pWindow);
    glfwTerminate();
    return result;
}
