#define DMOD_ENABLE_REGISTRATION    ON
#include "dmsdio_port.h"
#include "dmod.h"
#include "../stm32_common/stm32_common.h"

/*
 * STM32F4 SDIO (RM0090 section 31). The host logic is shared with STM32F7
 * in stm32_common; this file only describes the F4 instance and IRQ.
 */

/* ---- Family description ---- */

const stm32_sdio_instance_desc_t stm32_sdio_instances[] =
{
    /* SDIO: APB2 0x40012C00, RCC_APB2ENR.SDIOEN (bit 11), SDIO_IRQn 49,
     * DMA2 stream 3 or 6, channel 4 (RM0090 table 43) */
    { 0x40012C00UL, 11U, 49U, 1U, { 3U, 6U }, 4U },
};
const uint8_t stm32_sdio_instance_count = sizeof(stm32_sdio_instances) / sizeof(stm32_sdio_instances[0]);

/* STBITERR (start bit not detected on all data lines in wide bus mode) exists on F4 only. */
const uint32_t stm32_sdio_family_error_flags = STM32_SDIO_STA_STBITERR;

/* Cortex-M4: no data cache, nothing to maintain around DMA. */
const bool stm32_sdio_family_has_dcache = false;

/* The DMA controllers cannot access the 64 KB CCM data RAM (RM0090 2.3.1). */
#define STM32F4_CCM_START   0x10000000UL
#define STM32F4_CCM_END     0x10010000UL

bool stm32_sdio_family_dma_reachable(const void* address, size_t length)
{
    uintptr_t start = (uintptr_t)address;
    return (start + length <= STM32F4_CCM_START) || (start >= STM32F4_CCM_END);
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

DMOD_IRQ_HANDLER(49) { stm32_sdio_irq_handler(1); }     /* SDIO */
