/*
 * boot_udp.c
 *
 *  Created on: Dec 30, 2025
 *      Author: cprad
 */

#include "boot_proto.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include <string.h>
#include "flash_if.h"
#include "crc32.h"
#include "boot_jump.h"
#include "main.h"   // for LD2 LED debug
#include "stm32f7xx_hal.h"

#define BL_PORT 5000
#define APP_BASE     0x08040000u
#define APP_MAX_SIZE (0x08100000u - APP_BASE) // 0xC0000 = 786432 bytes (768KB)

static struct udp_pcb *pcb;
static uint32_t g_expected_size = 0;
static uint32_t g_expected_crc  = 0;
static uint32_t g_bytes_written = 0;
static uint32_t g_next_offset   = 0;
static volatile uint8_t g_do_jump = 0;
static volatile uint32_t g_jump_base = 0;



static void send_simple(struct udp_pcb *upcb, const ip_addr_t *addr, u16_t port,
                        uint8_t type, uint32_t seq, uint32_t arg0, uint32_t arg1)
{
  bl_hdr_t h = {0};
  h.magic = BL_MAGIC;
  h.type = type;
  h.hdr_len = (uint16_t)sizeof(bl_hdr_t);
  h.seq = seq;
  h.arg0 = arg0;
  h.arg1 = arg1;
  h.len = 0;

  struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, sizeof(h), PBUF_RAM);
  if (!q || q->len < sizeof(h)) { if (q) pbuf_free(q); return; }

  memcpy(q->payload, &h, sizeof(h));
  udp_sendto(upcb, q, addr, port);
  pbuf_free(q);
}

static void send_payload(struct udp_pcb *upcb, const ip_addr_t *addr, u16_t port,
                         uint8_t type, uint32_t seq, uint32_t arg0, uint32_t arg1,
                         const void *payload, uint32_t len)
{
  bl_hdr_t h = {0};
  h.magic = BL_MAGIC;
  h.type = type;
  h.hdr_len = (uint16_t)sizeof(bl_hdr_t);
  h.seq = seq;
  h.arg0 = arg0;
  h.arg1 = arg1;
  h.len = len;

  struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, sizeof(h) + len, PBUF_RAM);
  if (!q || q->tot_len < sizeof(h) + len) { if (q) pbuf_free(q); return; }

  pbuf_take(q, &h, sizeof(h));
  if (len && payload) {
      pbuf_take_at(q, payload, len, sizeof(h)); // if available
  }
  udp_sendto(upcb, q, addr, port);
  pbuf_free(q);
}

static void udp_rx_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port)
{
  (void)arg;
  if (!p) return;

  // Require at least header in one contiguous buffer
  if (p->tot_len < sizeof(bl_hdr_t)) { pbuf_free(p); return; }
  bl_hdr_t h;
  pbuf_copy_partial(p, &h, sizeof(h), 0);


  if (h.magic != BL_MAGIC || h.hdr_len != sizeof(bl_hdr_t)) {
    pbuf_free(p);
    return;
  }

  switch (h.type) {
    case BL_HELLO:
      // Reply with INFO (APP_BASE, APP_MAX_SIZE)
      send_simple(upcb, addr, port, BL_INFO, h.seq, APP_BASE, APP_MAX_SIZE);
      break;

    case BL_PEEK: {
        uint32_t a = h.arg0;
        uint32_t n = h.arg1;
        if (n > 64) n = 64;

        if (a < APP_BASE || a + n > (APP_BASE + APP_MAX_SIZE)) {
            send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 4); // bad addr
            break;
        }

        send_payload(upcb, addr, port, BL_PEEK_RESP, h.seq, a, n, (const void*)a, n);
        break;
    }

    case BL_DATA: {
        uint32_t off = h.arg0;       // offset from APP_BASE
        uint32_t n   = h.len;        // payload bytes
        uint32_t dst = APP_BASE + off;

        // Basic checks
        if (off > g_expected_size) {
            send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 5); // bad offset
            break;
        }
        if (n == 0 || n > 1024) {
            send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 6); // bad length
            break;
        }
        if (off + n > g_expected_size) {
            send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 7); // would exceed image
            break;
        }
        // Require payload present
        if (p->tot_len < sizeof(bl_hdr_t) + n) {
            send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 8); // missing payload
            break;
        }

        if ((dst & 3U) || (n & 3U)) {
            send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 9); // alignment
            break;
        }

        // copy payload into a local buffer (max 1024)
        uint8_t buf[1024];
        pbuf_copy_partial(p, buf, n, sizeof(bl_hdr_t));

        int st = flash_write(dst, buf, n);
        if (st != 0) {
            send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 10); // write failed
            break;
        }

        g_bytes_written += n;
        send_simple(upcb, addr, port, BL_ACK, h.seq, h.seq, 0);
        break;
    }

    case BL_END: {
        if (g_bytes_written != g_expected_size) {
            // arg0 = bytes_written, arg1 = expected_size for debug
            send_simple(upcb, addr, port, BL_ERR, h.seq, g_bytes_written, g_expected_size);
            break;
        }

        // Compute CRC over flash bytes
        const uint8_t *flash = (const uint8_t*)APP_BASE;
        uint32_t crc = crc32_zlib(flash, g_expected_size);

        if (crc != g_expected_crc) {
            // arg0 = computed CRC, arg1 = expected CRC
            send_simple(upcb, addr, port, BL_ERR, h.seq, crc, g_expected_crc);
            break;
        }

        // Success
        send_simple(upcb, addr, port, BL_OK, h.seq, h.seq, 0);

        g_jump_base = APP_BASE;
        g_do_jump = 1;
        break;
    }



    case BL_BEGIN:
      // Save expected image params
      g_expected_size = h.arg0;
      g_expected_crc  = h.arg1;
      g_bytes_written = 0;
      g_next_offset   = 0;

      // Basic bounds check
      if (g_expected_size == 0 || g_expected_size > APP_MAX_SIZE) {
        send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 1); // status=1 size bad
      } else {
    	  	  if (flash_erase_range(APP_BASE, g_expected_size) != 0) {
    	        send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 3); // 3 = erase failed
    	     } else {
    	        send_simple(upcb, addr, port, BL_ACK, h.seq, h.seq, 0);
    	     }
      }
      break;

    default:
      send_simple(upcb, addr, port, BL_ERR, h.seq, h.seq, 2); // unknown
      break;
  }

  pbuf_free(p);
}

void boot_udp_init(void)
{
  pcb = udp_new();
  if (!pcb) return;

  if (udp_bind(pcb, IP_ADDR_ANY, BL_PORT) != ERR_OK) {
    udp_remove(pcb);
    pcb = NULL;
    return;
  }

  udp_recv(pcb, udp_rx_cb, NULL);
}

void boot_udp_poll(void)
{
  if (!g_do_jump) return;

  // Clear flag first so we don't re-enter if something weird happens
  g_do_jump = 0;

  // Optional: visible "about to jump" indicator
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);



  //clear cache
//  SCB_DisableDCache();
//  SCB_DisableICache();

  // Jump (never returns if successful)
  boot_jump_to_app(g_jump_base);
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
}


