
#include <Interface/IMenu.h>

void IMenu::ClearFlag(uint32_t auiFlag)
{
    uiMenuFlags &= ~auiFlag;
}
