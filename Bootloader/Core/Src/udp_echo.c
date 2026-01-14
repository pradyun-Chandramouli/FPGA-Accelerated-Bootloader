#include "udp_echo.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

// protocol control block
static struct udp_pcb *pcb;

static void udp_rx_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    if (!p) return;
    // LED 2 turn on
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, 1);
    // destination IP - addr
    //desitnation port
    //p - packet payload(exact bytes)
    udp_sendto(upcb, p, addr, port);
    pbuf_free(p);

}

void udp_echo_init(uint16_t port)
{
    err_t e;
    //create a new protocol control block
    pcb = udp_new();
    if (!pcb) {
        // ERROR LED 3 RED ON
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, 1);
        return;
    }

    //bind the pcb to the port number we initialized too and bind it to any local ip(of the MCU)
    e = udp_bind(pcb, IP_ADDR_ANY, port);
    // if there is an error clean up and turn or error LED
    if (e != ERR_OK) {
        udp_remove(pcb);
        pcb = NULL;
        // ERROR LED 3 RED ON
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, 1);
        return;
    }

    // when a udp packet arrives for this pcb call udp_rx_cb function
    udp_recv(pcb, udp_rx_cb, NULL);
}
