#pragma once

#include "bsp_button.h"
#include "plugin_store.h"
#include "plugin_vm.h"

#include "esp_err.h"

#include <stdbool.h>

esp_err_t plugin_host_system_init(bool audio_available);
esp_err_t plugin_host_start(const plugin_record_t *record);
void plugin_host_stop(void);
plugin_vm_result_t plugin_host_dispatch(plugin_event_t event);
bool plugin_host_handle_key(bsp_btn_t button, bsp_btn_ev_t event);
bool plugin_host_take_exit_request(void);
