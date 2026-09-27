#define DMOD_ENABLE_REGISTRATION    ON
#include "dmsdio_port.h"
#include "dmod.h"
#include "dmclk_port.h"
#include "stm32f7_sdmmc_regs.h"

/* Single hardware instance supported today (SDMMC1) - `instance` is
 * accepted (and validated) for API symmetry with every other multi-instance
 * dmod port, but every register access below goes straight to
 * STM32F7_SDMMC1 rather than indexing an instance table. */
static int validate_instance(dmsdio_instance_t instance)
{
    return instance == 0 ? 0 : -1;
}

/* Generous, fixed iteration bounds for polling loops - real hardware and a
 * correctly wired card complete every one of these in a handful of bus
 * cycles; only a genuinely wedged bus or a removed card ever exhausts one,
 * and the caller (dmsdio.c) has its own retry/timeout policy on top of
 * this being a hard, bounded backstop rather than the primary timeout. */
#define SPIN_ITERATIONS  1000000UL

static uint32_t block_size_log2(uint32_t block_size)
{
    uint32_t log2 = 0;
    while ((1UL << log2) < block_size)
    {
        log2++;
    }
    return log2;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    Dmod_Printf("dmsdio port module initialized (stm32f7)\n");
    return 0;
}

int dmod_deinit(void)
{
    Dmod_Printf("dmsdio port module deinitialized (stm32f7)\n");
    return 0;
}

/* ---- Lifecycle ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _init, ( dmsdio_instance_t instance ))
{
    if (validate_instance(instance) != 0)
    {
        return -1;
    }

    STM32F7_RCC_APB2ENR |= RCC_APB2ENR_SDMMC1EN;

    /* Power-cycle sequence per RM0385 "SDMMC card power supply
     * management" - off, configure a safe default clock (disabled,
     * slowest divider, 1-bit), then on, and hold long enough for the card
     * to see well over the mandatory 74 SDMMC_CK cycles before the first
     * command (CMD0) goes out. */
    STM32F7_SDMMC1->POWER = SDMMC_POWER_PWRCTRL_OFF;
    for (volatile uint32_t i = 0; i < 10000UL; i++) { }

    STM32F7_SDMMC1->CLKCR = SDMMC_CLKCR_CLKDIV_Msk; /* clock disabled, slowest divider, 1-bit */
    STM32F7_SDMMC1->POWER = SDMMC_POWER_PWRCTRL_ON;
    for (volatile uint32_t i = 0; i < 10000UL; i++) { }

    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _deinit, ( dmsdio_instance_t instance ))
{
    if (validate_instance(instance) != 0)
    {
        return -1;
    }

    STM32F7_SDMMC1_IDMACTRL = 0;
    STM32F7_SDMMC1->POWER = SDMMC_POWER_PWRCTRL_OFF;
    STM32F7_RCC_APB2ENR &= ~RCC_APB2ENR_SDMMC1EN;
    return 0;
}

/* ---- Bus configuration ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _set_bus_width, ( dmsdio_instance_t instance, dmsdio_bus_width_t width ))
{
    if (validate_instance(instance) != 0)
    {
        return -1;
    }

    uint32_t clkcr = STM32F7_SDMMC1->CLKCR;
    clkcr &= ~SDMMC_CLKCR_WIDBUS_Msk;
    clkcr |= (width == dmsdio_bus_width_4bit) ? SDMMC_CLKCR_WIDBUS_4BIT : SDMMC_CLKCR_WIDBUS_1BIT;
    STM32F7_SDMMC1->CLKCR = clkcr;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_speed_mode, ( dmsdio_instance_t instance, dmsdio_speed_mode_t mode ))
{
    if (validate_instance(instance) != 0)
    {
        return -1;
    }

    uint32_t target_hz;
    switch (mode)
    {
        case dmsdio_speed_mode_identification: target_hz = 400000UL;   break;
        case dmsdio_speed_mode_high_speed:      target_hz = 50000000UL; break;
        case dmsdio_speed_mode_default_speed:
        default:                                target_hz = 25000000UL; break;
    }

    /* SDMMC_CK = SDIOCLK / (CLKDIV + 2) - see RM0385 CLKCR - so the
     * divider for a given target is (SDIOCLK / target) - 2, clamped to
     * what the 8-bit field can hold and never allowed to go negative
     * (which would mean "target is faster than this port can reach"). */
    dmclk_frequency_t sdioclk = dmclk_port_get_domain_frequency(dmclk_domain_sdio);

    uint32_t clkdiv = 0xFFUL; /* slowest possible until proven otherwise */
    if (sdioclk > 0)
    {
        uint64_t divider = sdioclk / target_hz;
        clkdiv = (divider > 2ULL) ? (uint32_t)(divider - 2ULL) : 0UL;
        if (clkdiv > 0xFFUL)
        {
            clkdiv = 0xFFUL;
        }
    }

    uint32_t clkcr = STM32F7_SDMMC1->CLKCR;
    clkcr &= ~SDMMC_CLKCR_CLKDIV_Msk;
    clkcr |= (clkdiv << SDMMC_CLKCR_CLKDIV_Pos) | SDMMC_CLKCR_CLKEN | SDMMC_CLKCR_HWFC_EN;
    STM32F7_SDMMC1->CLKCR = clkcr;
    return 0;
}

/* ---- Commands ---- */

dmod_dmsdio_port_api_declaration(1.0, dmsdio_error_t, _send_command,
    ( dmsdio_instance_t instance, uint8_t cmd_index, uint32_t argument,
      dmsdio_response_type_t response_type, dmsdio_response_t *response ))
{
    if (validate_instance(instance) != 0)
    {
        return dmsdio_error_invalid_argument;
    }

    STM32F7_SDMMC1->ICR = SDMMC_STATIC_FLAGS;
    STM32F7_SDMMC1->ARG = argument;

    uint32_t waitresp = (response_type == dmsdio_response_none) ? SDMMC_CMD_WAITRESP_NONE
                       : (response_type == dmsdio_response_r2)  ? SDMMC_CMD_WAITRESP_LONG
                       :                                          SDMMC_CMD_WAITRESP_SHORT;

    STM32F7_SDMMC1->CMD = ((uint32_t)cmd_index & SDMMC_CMD_CMDINDEX_Msk) | waitresp | SDMMC_CMD_CPSMEN;

    uint32_t wait_mask = (response_type == dmsdio_response_none)
        ? SDMMC_STA_CMDSENT
        : (SDMMC_STA_CMDREND | SDMMC_STA_CTIMEOUT | SDMMC_STA_CCRCFAIL);

    uint32_t sta = 0;
    uint32_t timeout = SPIN_ITERATIONS;
    do
    {
        sta = STM32F7_SDMMC1->STA;
    } while ((sta & wait_mask) == 0 && --timeout > 0);

    STM32F7_SDMMC1->ICR = SDMMC_STATIC_FLAGS;

    if (timeout == 0 || (sta & SDMMC_STA_CTIMEOUT) != 0)
    {
        return dmsdio_error_no_response;
    }
    /* R3's CRC field is defined by the spec to be all 1s - a real card
     * always fails this check for it, so it must never be treated as an
     * error for that response type specifically. */
    if ((sta & SDMMC_STA_CCRCFAIL) != 0 && response_type != dmsdio_response_r3)
    {
        return dmsdio_error_command_crc;
    }

    if (response != NULL)
    {
        if (response_type == dmsdio_response_r2)
        {
            response->words[0] = STM32F7_SDMMC1->RESP1;
            response->words[1] = STM32F7_SDMMC1->RESP2;
            response->words[2] = STM32F7_SDMMC1->RESP3;
            response->words[3] = STM32F7_SDMMC1->RESP4;
        }
        else
        {
            response->words[0] = 0;
            response->words[1] = 0;
            response->words[2] = 0;
            response->words[3] = STM32F7_SDMMC1->RESP1;
        }
    }

    return dmsdio_error_none;
}

/* ---- Data transfer (SDMMC's own internal IDMA - no dmdma involved) ---- */

static dmsdio_error_t run_data_phase(void *buffer, uint32_t block_size, uint32_t block_count, bool is_write)
{
    STM32F7_SDMMC1->ICR = SDMMC_STATIC_FLAGS;
    STM32F7_SDMMC1->DTIMER = 0xFFFFFFFFUL; /* see SPIN_ITERATIONS - the real timeout bound is the software spin below */
    STM32F7_SDMMC1->DLEN = block_size * block_count;
    STM32F7_SDMMC1->DCTRL = (block_size_log2(block_size) << SDMMC_DCTRL_DBLOCKSIZE_Pos)
                           | (is_write ? SDMMC_DCTRL_DTDIR_WRITE : SDMMC_DCTRL_DTDIR_READ)
                           | SDMMC_DCTRL_DTMODE_BLOCK
                           | SDMMC_DCTRL_DTEN;

    STM32F7_SDMMC1_IDMABASE0 = (uint32_t)(uintptr_t)buffer;
    STM32F7_SDMMC1_IDMACTRL = SDMMC_IDMACTRL_IDMAEN;

    uint32_t wait_mask = SDMMC_STA_DATAEND | SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT | SDMMC_STA_TXUNDERR | SDMMC_STA_RXOVERR;
    uint32_t sta = 0;
    uint32_t timeout = SPIN_ITERATIONS;
    do
    {
        sta = STM32F7_SDMMC1->STA;
    } while ((sta & wait_mask) == 0 && --timeout > 0);

    STM32F7_SDMMC1_IDMACTRL = 0;
    STM32F7_SDMMC1->ICR = SDMMC_STATIC_FLAGS;

    if (timeout == 0 || (sta & SDMMC_STA_DTIMEOUT) != 0)
    {
        return dmsdio_error_data_timeout;
    }
    if ((sta & (SDMMC_STA_DCRCFAIL | SDMMC_STA_TXUNDERR | SDMMC_STA_RXOVERR)) != 0)
    {
        return dmsdio_error_data_crc;
    }

    return dmsdio_error_none;
}

dmod_dmsdio_port_api_declaration(1.0, dmsdio_error_t, _read_blocks,
    ( dmsdio_instance_t instance, void *buffer, uint32_t block_size, uint32_t block_count ))
{
    if (validate_instance(instance) != 0 || buffer == NULL || block_size == 0 || block_count == 0)
    {
        return dmsdio_error_invalid_argument;
    }

    return run_data_phase(buffer, block_size, block_count, false);
}

dmod_dmsdio_port_api_declaration(1.0, dmsdio_error_t, _write_blocks,
    ( dmsdio_instance_t instance, const void *buffer, uint32_t block_size, uint32_t block_count ))
{
    if (validate_instance(instance) != 0 || buffer == NULL || block_size == 0 || block_count == 0)
    {
        return dmsdio_error_invalid_argument;
    }

    return run_data_phase((void *)buffer, block_size, block_count, true);
}
