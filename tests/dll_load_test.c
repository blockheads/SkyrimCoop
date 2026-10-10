#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    const char* dll_path = argc > 1 ? argv[1] : "SkyrimTogetherClient.dll";
    printf("Attempting LoadLibrary(\"%s\")...\n", dll_path);
    fflush(stdout);

    HMODULE hMod = LoadLibraryA(dll_path);
    if (hMod) {
        printf("SUCCESS: DLL loaded at %p\n", (void*)hMod);

        void* pLoad = GetProcAddress(hMod, "SKSEPlugin_Load");
        void* pVer = GetProcAddress(hMod, "SKSEPlugin_Version");
        printf("SKSEPlugin_Load: %p\n", pLoad);
        printf("SKSEPlugin_Version: %p\n", pVer);

        FreeLibrary(hMod);
    } else {
        DWORD err = GetLastError();
        printf("FAILED: error %lu (0x%lX)\n", err, err);
    }
    return 0;
}
