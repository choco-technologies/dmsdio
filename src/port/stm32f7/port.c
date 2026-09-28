#define DMOD_ENABLE_REGISTRATION    ON
#include "dmsdio_port.h"
#include "dmod.h"
#include "../stm32_common/stm32_common.h"

/*
 * STM32F7 SDMMC (RM0385 section 35 / RM0410 section 35). The host logic is
 * shared with STM32F4 in stm32_common; this file only describes the F7
 * instance and IRQ.
 */

/* ---- Family description ---- */

const stm32_sdio_instance_desc_t stm32_sdio_instances[] =
{
    /* SDMMC1: APB2 0x40012C00, RCC_APB2ENR.SDMMC1EN (bit 11), SDMMC1_IRQn 49,
     * DMA2 stream 3 or 6, channel 4 (RM0385 table 27) */
    { 0x40012C00UL, 11U, 49U, 1U, { 3U, 6U }, 4U },
    /*
     * SDMMC2 (0x40011C00, APB2ENR bit 7, IRQ 103) exists only on F76x/F77x.
     * Packages are built per CPU family, so registering its IRQ would make
     * the loader warn on every F74x/F75x (IRQ table of 98 entries). Not
     * supported until the port can be specialized per MCU.
     */
};
const uint8_t stm32_sdio_instance_count = sizeof(stm32_sdio_instances) / sizeof(stm32_sdio_instances[0]);

/* SDMMC has no STBITERR (STA bit 9 is reserved on F7). */
const uint32_t stm32_sdio_family_error_flags = 0U;

/* DMA2 reaches SRAM1/2, DTCM (through the AHBS port) and external memory -
 * only ITCM RAM (0x00000000-0x00003FFF) is out of reach, and no data buffer
 * lives there. */
#define STM32F7_ITCM_END    0x00004000UL

bool stm32_sdio_family_dma_reachable(const void* address, size_t length)
{
    (void)length;
    return (uintptr_t)address >= STM32F7_ITCM_END;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    stm32_sdio_common_init();
    return 0;
}

int dmod_deinit(void)
{
    stm32_sdio_common_deinit();
    return 0;
}

/* ---- IRQ handlers ---- */

DMOD_IRQ_HANDLER(49)  { stm32_sdio_irq_handler(1); }    /* SDMMC1 */
