// tools/simulator/host/include/esp_lcd_panel_io.h
// Host-side shim of the panel IO command channel. PokeWalk sends a NOP command
// between bands to wait for the (real, asynchronous) SPI DMA to drain; on the
// desktop the blit is synchronous, so the command is a no-op.
#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd,
                                    const void *param, size_t param_size);
