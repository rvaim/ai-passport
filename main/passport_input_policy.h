#pragma once

#include "bsp_button.h"

/* Native list navigation treats a component-level DOUBLE as two deliberate
 * moves. OK remains action-only, and PRESS/LONG never change selection. */
static inline int passport_input_navigation_delta(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    const int distance = ev == BSP_BTN_CLICK ? 1 : ev == BSP_BTN_DOUBLE ? 2 : 0;
    if (btn == BSP_BTN_UP) return -distance;
    if (btn == BSP_BTN_DOWN) return distance;
    return 0;
}
