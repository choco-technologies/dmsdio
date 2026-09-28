#define DMOD_ENABLE_REGISTRATION    ON
#include "dmsdio_port.h"
#include "dmod.h"
#include "../stm32_common/stm32_common.h"

/*
 * STM32F7 SDMMC (RM0385 section 35 / RM0410 section 35). The host logic is
 * shared with STM32F4 in stm32_common; this file only describes the F7
 * instances and IRQs.
 */

/* ---- Family description ---- */

const stm32_sdio_instance_desc_t stm32_sdio_instances[] =
{
    /* SDMMC1: APB2 0x40012C00, RCC_APB2ENR.SDMMC1EN (bit 11), SDMMC1_IRQn 49 */
    { 0x40012C00UL, 11U, 49U },
    /* SDMMC2 (STM32F76x/F77x only): APB2 0x40011C00, RCC_APB2ENR.SDMMC2EN (bit 7), SDMMC2_IRQn 103 */
    { 0x40011C00UL, 7U, 103U },
};
const uint8_t stm32_sdio_instance_count = sizeof(stm32_sdio_instances) / sizeof(stm32_sdio_instances[0]);

/* SDMMC has no STBITERR (STA bit 9 is reserved on F7). */
const uint32_t stm32_sdio_family_error_flags = 0U;

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
DMOD_IRQ_HANDLER(103) { stm32_sdio_irq_handler(2); }    /* SDMMC2 */
