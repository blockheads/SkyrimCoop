#pragma once

/**
 * @brief Leaves out the hooks of chosen files, to bisect a crash to the hooks that cause it
 * (`scripts/coop_test.py bisect-hooks`).
 *
 * SKYRIMCOOP_DISABLE_HOOKS=Actor,Renderer: the initializers in Actor.cpp and Renderer.cpp (file
 * names without directory or extension) still run, so the engine functions they look up are set,
 * but install no hook and patch no byte. Each hook initializer starts with
 * `const HookGate cGate(__FILE__);`.
 */
struct HookGate
{
    explicit HookGate(const char* acpFile) noexcept;
    ~HookGate() noexcept;

    HookGate(const HookGate&) = delete;
    HookGate& operator=(const HookGate&) = delete;

private:
    bool m_suppressing{};
};
