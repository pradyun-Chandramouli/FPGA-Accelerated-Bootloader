#include "stm32f7xx_hal.h"
#include "boot_jump.h"

static void nvic_full_reset(void)
{
  for (uint32_t i = 0; i < 8; i++) {
    NVIC->ICER[i] = 0xFFFFFFFF;  // disable IRQs
    NVIC->ICPR[i] = 0xFFFFFFFF;  // clear pending
  }
  __DSB(); __ISB();
}

void boot_jump_to_app(uint32_t app_base)
{

  uint32_t msp = *(volatile uint32_t *)(app_base + 0);
  uint32_t rh  = *(volatile uint32_t *)(app_base + 4);

//  if ((msp & 0x2FFE0000u) != 0x20000000u) return;

//  if ((rh & 1u) == 0u) return;

  __disable_irq();

  SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
  __DSB(); __ISB();

  // stop SysTick
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  // disable + clear all NVIC interrupts
  nvic_full_reset();

  // clear fault status
  SCB->CFSR = 0xFFFFFFFF;
  SCB->HFSR = 0xFFFFFFFF;
  SCB->DFSR = 0xFFFFFFFF;
  SCB->MMFAR = 0;
  SCB->BFAR  = 0;

  // set vector table + stack  jump
  SCB->VTOR = app_base;
  __DSB(); __ISB();

  __set_MSP(msp);
  ((void (*)(void))rh)();

  while (1) {}
}


