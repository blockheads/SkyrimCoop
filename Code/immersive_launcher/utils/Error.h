#pragma once

#include <windows.h>
#include <commctrl.h>

#include <TiltedCore/Stl.hpp>

void Die(const wchar_t* aText, bool aKillNow = false);

void ShowProgressDialog();
