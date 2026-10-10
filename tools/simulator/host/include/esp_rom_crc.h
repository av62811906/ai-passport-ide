// tools/simulator/host/include/esp_rom_crc.h
// Host-side shim of the ROM CRC helper. PokeWalk's sensing layer takes it
// unconditionally; the host implementation is the standard CRC-32 (zlib), which
// is what the device ROM routine computes.
#pragma once

#include <stdint.h>

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);
