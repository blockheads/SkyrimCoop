#pragma once

// MinGW x64 calling convention compatibility.
// On x64, __fastcall/__stdcall/__cdecl all map to the same MS x64 calling convention.
// GCC expands these to __attribute__ forms that break 'using' type alias declarations
// (e.g., 'using T = void(__fastcall)()' is a parse error in GCC).
// This header undefs and redefines them as empty macros.
// Must be included AFTER all system headers (especially windows.h) which define them.
#if defined(__GNUC__) && defined(__x86_64__)
#undef __fastcall
#define __fastcall
#undef __stdcall
#define __stdcall
#undef __cdecl
#define __cdecl
#endif
