#include "HeadlessDisplay.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

HeadlessDisplay::~HeadlessDisplay()
{
    if (m_process > 0)
    {
        kill(m_process, SIGTERM);
        waitpid(m_process, nullptr, 0);
    }
}

bool HeadlessDisplay::Start()
{
    int pipeEnds[2];
    if (pipe(pipeEnds) != 0)
        return false;
    m_process = fork();
    if (m_process == 0)
    {
        close(pipeEnds[0]);
        const auto descriptor = std::to_string(pipeEnds[1]);
        execlp("Xvfb", "Xvfb", "-displayfd", descriptor.c_str(), "-screen", "0", "3840x2160x24", "-nolisten", "tcp",
               "-ac", nullptr);
        _exit(127);
    }
    close(pipeEnds[1]);
    pollfd ready{pipeEnds[0], POLLIN, 0};
    char number[32]{};
    const int count = m_process > 0 && poll(&ready, 1, 10000) > 0 ? read(pipeEnds[0], number, sizeof(number) - 1) : -1;
    close(pipeEnds[0]);
    if (count <= 0)
        return false;
    const auto display = ":" + std::to_string(std::atoi(number));
    setenv("DISPLAY", display.c_str(), 1);
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    return true;
}
