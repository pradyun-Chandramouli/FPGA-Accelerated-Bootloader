#pragma once
#include <stdint.h>

#define BL_MAGIC 0xB00710ADu

typedef enum {
  BL_HELLO = 1,
  BL_INFO  = 2,
  BL_BEGIN = 3,
  BL_DATA  = 4,
  BL_END   = 5,
  BL_ACK   = 6,
  BL_ERR   = 7,
  BL_OK    = 8,
  BL_PEEK = 9,
  BL_PEEK_RESP = 10,
} bl_msg_t;

#pragma pack(push, 1)
typedef struct {
  uint32_t magic;   // BL_MAGIC
  uint8_t  type;    // bl_msg_t
  uint8_t  rsv0;
  uint16_t hdr_len; // sizeof(bl_hdr_t)
  uint32_t seq;     // increments per message from PC
  uint32_t arg0;    // meaning depends on type
  uint32_t arg1;    // meaning depends on type
  uint32_t len;     // payload bytes following header
} bl_hdr_t;
#pragma pack(pop)
