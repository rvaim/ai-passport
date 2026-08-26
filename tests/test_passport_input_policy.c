#include "passport_input_policy.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(passport_input_navigation_delta(BSP_BTN_UP, BSP_BTN_CLICK) == -1);
    assert(passport_input_navigation_delta(BSP_BTN_DOWN, BSP_BTN_CLICK) == 1);
    assert(passport_input_navigation_delta(BSP_BTN_UP, BSP_BTN_DOUBLE) == -2);
    assert(passport_input_navigation_delta(BSP_BTN_DOWN, BSP_BTN_DOUBLE) == 2);
    assert(passport_input_navigation_delta(BSP_BTN_OK, BSP_BTN_DOUBLE) == 0);
    assert(passport_input_navigation_delta(BSP_BTN_DOWN, BSP_BTN_PRESS) == 0);
    assert(passport_input_navigation_delta(BSP_BTN_UP, BSP_BTN_LONG) == 0);
    puts("Passport input policy host tests: PASS");
    return 0;
}
