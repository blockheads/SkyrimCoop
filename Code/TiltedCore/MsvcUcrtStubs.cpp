// MsvcUcrtStubs.cpp — Stubs for MSVC UCRT functions referenced by
// third-party static libraries (e.g., libuv) compiled with MSVC.
// Required for MinGW GCC 10 cross-compilation where these functions
// are not provided by the runtime headers/libs.
// MinGW 14.3+ provides these natively; this file is a compatibility shim.

#ifdef __MINGW32__

#include <cstdint>
#include <cstdarg>
#include <cwchar>
#include <cstdio>
#include <csetjmp>

extern "C" {

unsigned long long __local_stdio_printf_options(void)
{
    // UCRT internal: returns format options bitmask (0 = default behavior)
    return 0;
}

int __stdio_common_vsnwprintf_s(
    unsigned long long _Options,
    wchar_t* _Buffer,
    size_t _BufferCount,
    size_t _MaxCount,
    const wchar_t* _Format,
    void* _Locale,
    va_list _ArgList)
{
    (void)_Options;
    (void)_Locale;
    // Forward to MinGW's vsnwprintf which is the equivalent
    if (_Buffer && _BufferCount > 0)
    {
        size_t count = (_MaxCount < _BufferCount) ? _MaxCount : _BufferCount;
        int result = _vsnwprintf(_Buffer, count, _Format, _ArgList);
        // Ensure null termination
        _Buffer[_BufferCount - 1] = L'\0';
        return result;
    }
    return -1;
}

// __intrinsic_setjmpex is an MSVC intrinsic used by Lua's longjmp mechanism.
// On MinGW/GCC, we forward to the standard setjmp implementation.
// This satisfies the linker for MSVC-compiled Lua packages.
int __intrinsic_setjmpex(void* _Buf)
{
    // Forward to MinGW's _setjmp (which takes a context pointer as 2nd arg)
    return _setjmp(static_cast<jmp_buf*>(_Buf)[0], nullptr);
}

} // extern "C"

#endif // __MINGW32__
