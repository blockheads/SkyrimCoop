#include <Interface/ConsoleLines.h>

#include <VersionDb.h>

void ConsoleLines::Execute(const char* acpLine) noexcept
{
    using TExecuteCommand = void(const char*);
    POINTER_SKYRIMSE(TExecuteCommand, s_executeCommand, 442672); // Console::ExecuteCommand
    s_executeCommand.Get()(acpLine);
}
