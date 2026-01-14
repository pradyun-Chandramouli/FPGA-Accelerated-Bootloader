#include "flash_if.h"
#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_flash_ex.h"

static uint32_t flash_get_sector(uint32_t address)
{
    // Bank 1
    if (address < 0x08100000U) {
        if (address < 0x08008000U) return FLASH_SECTOR_0;
        if (address < 0x08010000U) return FLASH_SECTOR_1;
        if (address < 0x08018000U) return FLASH_SECTOR_2;
        if (address < 0x08020000U) return FLASH_SECTOR_3;
        if (address < 0x08040000U) return FLASH_SECTOR_4;
        if (address < 0x08080000U) return FLASH_SECTOR_5;
        if (address < 0x080C0000U) return FLASH_SECTOR_6;
        return FLASH_SECTOR_7;
    }

    // Bank 2
    if (address < 0x08108000U) return FLASH_SECTOR_8;
    if (address < 0x08110000U) return FLASH_SECTOR_9;
    if (address < 0x08118000U) return FLASH_SECTOR_10;
    if (address < 0x08120000U) return FLASH_SECTOR_11;
    if (address < 0x08140000U) return FLASH_SECTOR_12;
    if (address < 0x08180000U) return FLASH_SECTOR_13;
    if (address < 0x081C0000U) return FLASH_SECTOR_14;
    return FLASH_SECTOR_15;
}

int flash_erase_range(uint32_t start_addr, uint32_t length)
{
	if (length == 0) return -2;
	uint32_t end_addr = start_addr + length - 1U;
	if (start_addr < FLASH_BASE || end_addr > FLASH_END) return -3;


    uint32_t first = flash_get_sector(start_addr);
    uint32_t last  = flash_get_sector(end_addr);

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_err = 0;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector       = first;
    erase.NbSectors    = (last - first) + 1U;

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &sector_err);
    HAL_FLASH_Lock();

    return (st == HAL_OK) ? 0 : -1;
}

int flash_write(uint32_t dst_addr, const uint8_t *src, uint32_t len)
{
    // So dst_addr must be 4-byte aligned and len multiple of 4
    if ((dst_addr & 3U) || (len & 3U)) return -2;

    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t w =  (uint32_t)src[i]
                    | ((uint32_t)src[i+1] << 8)
                    | ((uint32_t)src[i+2] << 16)
                    | ((uint32_t)src[i+3] << 24);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_addr + i, w) != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}
