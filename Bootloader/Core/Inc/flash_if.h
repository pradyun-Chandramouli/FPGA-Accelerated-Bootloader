#pragma once
#include <stdint.h>

int flash_erase_range(uint32_t start_addr, uint32_t length);
int flash_write(uint32_t dst_addr, const uint8_t *src, uint32_t len);
