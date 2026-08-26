#pragma once

#include "bsp_button.h"
#include "esp_err.h"
#include "passport_app_registry.h"
#include "passport_link_protocol.h"
#include <stdbool.h>

/** Start one Lua app. Only one foreground runtime exists at a time. */
esp_err_t passport_runtime_start(const passport_app_info_t *app);
void passport_runtime_stop(void);
bool passport_runtime_running(void);

/** Dispatch three-key events from the system UI task, never directly from the BSP button callback. */
void passport_runtime_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);

/** Dispatch an already target-validated Passport Link frame to the foreground app namespace. */
void passport_runtime_handle_link(const passport_link_frame_t *frame);
