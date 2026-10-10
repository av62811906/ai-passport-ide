#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RESTORE_IMAGE_SIZE 0x6000u
#define RESTORE_STAGE_SIZE 0x10000u
// stage=false addresses NVS; stage=true addresses the isolated restore partition.
typedef struct {
 bool (*read)(bool stage,uint32_t at,void *data,size_t size);
 bool (*write)(bool stage,uint32_t at,const void *data,size_t size);
 bool (*erase)(bool stage,uint32_t at,size_t size);
} restore_io_t;
uint32_t restore_crc32(const uint8_t *data,size_t size);
bool restore_journal_prepare(const restore_io_t *io,const uint8_t *image,const uint8_t build[32]);
// Called before NVS/Wi-Fi/world initialization. 0 none; 1 restored; 2 rolled back;
// -1 durable request cannot yet be recovered: caller must not start the game.
int restore_journal_apply(const restore_io_t *io,const uint8_t build[32],uint32_t *crc);
