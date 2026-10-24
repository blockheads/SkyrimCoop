#pragma once

struct HeadlessDisplay
{
    ~HeadlessDisplay();
    bool Start();

  private:
    int m_process = -1;
};
