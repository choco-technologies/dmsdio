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
    /* SDIO: APB2 0x40012C00, RCC_APB2ENR.SDIOEN (bit 11), SDIO_IRQn 49 */
    { 0x40012C00UL, 11U, 49U },
};
const uint8_t stm32_sdio_instance_count = sizeof(stm32_sdio_instances) / sizeof(stm32_sdio_instances[0]);

/* STBITERR (start bit not detected on all data lines in wide bus mode) exists on F4 only. */
const uint32_t stm32_sdio_family_error_flags = STM32_SDIO_STA_STBITERR;

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
