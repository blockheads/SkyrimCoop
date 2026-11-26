#pragma once

// The console's text (CommonLibSSE-NG ConsoleLog): VPrint (51110, `(ConsoleLog*, const char* fmt, va_list)`, IDA
// 2026-10-21) formats each line into lastMessage, then appends it to the history.
struct ConsoleLog
{
    uint8_t pad0;
    char lastMessage[0x400]; // 001
};
static_assert(offsetof(ConsoleLog, lastMessage) == 0x1);
