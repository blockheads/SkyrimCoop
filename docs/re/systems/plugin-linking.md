# Plugin linking and hook registration

Most hooks register themselves from a file-scope initializer:

```cpp
static Hooks::Initializer s_hooks([]() { HOOK(&RealX, HookX); });
```

Nothing references these files' symbols, so **a static-archive link drops them**: the linker
only pulls archive members that resolve an undefined symbol. Until 2026-10-10 the MinGW DLL
linked `libSkyrimTogetherClient.a` and silently lost 38 of 121 client objects, including the
main-loop tick (`SkyrimVM64.cpp`), the crash handler, magic, UI, input and Papyrus hooks.
The MSVC build avoided this with `/WHOLEARCHIVE`.

Fix: on MinGW the client is an xmake `object` target (`Code/client/xmake.lua`), so every object
is linked. To check after build changes:

```bash
grep -oE "objs/SkyrimTogetherClient/[^:( ]*\.obj" SkyrimTogetherClient.map | sort -u | wc -l
# must equal the number of client .cpp files compiled
```
