/* DMOD_ENABLE_REGISTRATION is intentionally NOT set here: each family's
 * port.c (which includes dmsdio_port.h with the flag on) is the single
 * translation unit that emits this module's API registration table. See
 * dmspi/src/port/stm32_common/stm32_common.c for the same split. */
#include "dmod.h"
#include "dmosi.h"
#include "dmclk_port.h"
#include "stm32_common.h"
#include <errno.h>
#include <string.h>

/*
 * STM32F4 SDIO / STM32F7 SDMMC host - protocol-free primitives only.
 *
 * Command path: CPSM with CMDREND/CMDSENT/CCRCFAIL/CTIMEOUT interrupts.
 * Data path: DPSM in block mode, FIFO serviced from the interrupt handler
 * (RXFIFOHF / TXFIFOHE, 8 words per interrupt) until DATAEND or a data
 * error. For reads the DPSM and the FIFO interrupts are armed before the
 * command is sent so no block can overrun the 32-word FIFO while the
 * response is being processed; for writes the FIFO is only fed after a
 * valid response, as the card expects.
 *
 * DMA is not used: SDIO/SDMMC data requests need a DMA stream in FIFO mode
 * with 4-beat bursts and peripheral flow control (RM0090 31.3.2 / RM0385
 * 35.3.2), which the dmdma lease API does not expose yet. Hardware flow
 * control (CLKCR.HWFC_EN) is not used either: ES0182 (STM32F40x/41x errata)
 * documents SDIO_CK glitches with it enabled.
 *
 * Every wait is bounded: the command wait by STM32_CMD_TIMEOUT_MS on top of
 * the hardware 64-clock response timeout, the data wait by the per-block
 * timeout requested by the core (also programmed into DTIMER).
 */

#define STM32_CMD_TIMEOUT_MS        100
#define STM32_DATA_TIMEOUT_SLACK_MS 50

typedef struct
{
    bool                initialized;
    dmosi_semaphore_t   done;           /* posted by the ISR per finished phase */
    uint32_t            clock_hz;       /* current SDIO_CK */
    volatile bool       cmd_busy;
    volatile uint32_t   cmd_status;
    volatile bool       data_busy;
    volatile uint32_t   data_status;
    volatile bool       data_read;
    uint32_t* volatile  cursor;         /* next FIFO word in the caller's buffer */
    volatile uint32_t   words_left;
} stm32_sdio_state_t;

static stm32_sdio_state_t g_state[STM32_SDIO_MAX_INSTANCES];

/* ======================================================================
 *  Helpers
 * ====================================================================== */

static const stm32_sdio_instance_desc_t* get_desc(dmsdio_instance_t instance)
{
    if (instance < 1 || instance > stm32_sdio_instance_count || instance > STM32_SDIO_MAX_INSTANCES)
    {
        return NULL;
    }
    return &stm32_sdio_instances[instance - 1];
}

static volatile stm32_sdio_t* get_regs(dmsdio_instance_t instance)
{
    return (volatile stm32_sdio_t*)stm32_sdio_instances[instance - 1].base;
}

static stm32_sdio_state_t* get_ready_state(dmsdio_instance_t instance)
{
    if (get_desc(instance) == NULL || !g_state[instance - 1].initialized)
    {
        return NULL;
    }
    return &g_state[instance - 1];
}

static uint32_t data_error_flags(void)
{
    return STM32_SDIO_DATA_ERRORS | stm32_sdio_family_error_flags;
}

static uint32_t static_flags(void)
{
    return STM32_SDIO_STATIC_FLAGS | stm32_sdio_family_error_flags;
}

static void nvic_enable_irq(uint32_t irqn)
{
    /* Priority at or below dmosi's limit: the handler posts a semaphore. */
    volatile uint8_t  *nvic_ip   = (volatile uint8_t *)0xE000E400UL;
    volatile uint32_t *nvic_iser = (volatile uint32_t *)0xE000E100UL;
    nvic_ip[irqn] = (uint8_t)dmosi_get_min_interrupt_priority();
    nvic_iser[irqn >> 5U] = 1U << (irqn & 0x1FU);
}

static void nvic_disable_irq(uint32_t irqn)
{
    volatile uint32_t *nvic_icer = (volatile uint32_t *)0xE000E180UL;
    nvic_icer[irqn >> 5U] = 1U << (irqn & 0x1FU);
}

/* Registers written back to back need 3 SDIOCLK + 2 PCLK2 cycles (RM0090 31.9). */
static void register_settle(void)
{
    dmclk_port_delay_us(1);
}

/* MASK is also modified by the ISR - thread-side read-modify-write must not interleave. */
static void mask_enable(volatile stm32_sdio_t* regs, uint32_t bits)
{
    Dmod_EnterCritical();
    regs->MASK |= bits;
    Dmod_ExitCritical();
}

static void drain_semaphore(stm32_sdio_state_t* st)
{
    while (dmosi_semaphore_wait(st->done, 1, 0) == 0)
    {
    }
}

/* Wait until the ISR cleared *busy. Returns false on timeout. */
static bool wait_phase(stm32_sdio_state_t* st, volatile bool* busy, uint32_t timeout_ms)
{
    int32_t timeout = (timeout_ms > (uint32_t)INT32_MAX) ? INT32_MAX : (int32_t)timeout_ms;
    while (*busy)
    {
        if (dmosi_semaphore_wait(st->done, 1, timeout) != 0)
        {
            return !*busy;
        }
    }
    return true;
}

/* ======================================================================
 *  Lifecycle
 * ====================================================================== */

void stm32_sdio_common_init(void)
{
    memset(g_state, 0, sizeof(g_state));
}

void stm32_sdio_common_deinit(void)
{
    for (uint8_t i = 1; i <= STM32_SDIO_MAX_INSTANCES; i++)
    {
        if (get_ready_state(i) != NULL)
        {
            dmsdio_port_host_deinit(i);
        }
    }
}

static void reset_peripheral(const stm32_sdio_instance_desc_t* desc)
{
    STM32_RCC_APB2ENR  |= (1U << desc->apb2_bit);
    (void)STM32_RCC_APB2ENR;                    /* read back: clock active before access */
    STM32_RCC_APB2RSTR |= (1U << desc->apb2_bit);
    STM32_RCC_APB2RSTR &= ~(1U << desc->apb2_bit);
}

dmod_dmsdio_port_api_declaration(1.0, int, _host_init, ( dmsdio_instance_t instance ))
{
    const stm32_sdio_instance_desc_t* desc = get_desc(instance);
    if (desc == NULL)
    {
        DMOD_LOG_ERROR("dmsdio_port: no SDIO/SDMMC instance %u on this family\n", (unsigned)instance);
        return -ENODEV;
    }
    stm32_sdio_state_t* st = &g_state[instance - 1];
    if (st->initialized)
    {
        return -EBUSY;
    }
    if (dmclk_port_get_domain_frequency(dmclk_domain_sdio) == 0)
    {
        DMOD_LOG_ERROR("dmsdio_port: SDIO kernel clock (CLK48) is not running\n");
        return -EIO;
    }
    memset(st, 0, sizeof(*st));
    st->done = dmosi_semaphore_create(0, 2);
    if (st->done == NULL)
    {
        return -ENOMEM;
    }
    reset_peripheral(desc);
    volatile stm32_sdio_t* regs = get_regs(instance);
    regs->MASK  = 0;
    regs->ICR   = static_flags();
    regs->POWER = STM32_SDIO_POWER_OFF;
    regs->CLKCR = 0;
    nvic_enable_irq(desc->irqn);
    st->initialized = true;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _host_deinit, ( dmsdio_instance_t instance ))
{
    stm32_sdio_state_t* st = get_ready_state(instance);
    if (st == NULL)
    {
        return -ENODEV;
    }
    const stm32_sdio_instance_desc_t* desc = get_desc(instance);
    volatile stm32_sdio_t* regs = get_regs(instance);
    nvic_disable_irq(desc->irqn);
    regs->MASK  = 0;
    regs->DCTRL = 0;
    regs->CLKCR = 0;
    regs->POWER = STM32_SDIO_POWER_OFF;
    STM32_RCC_APB2ENR &= ~(1U << desc->apb2_bit);
    dmosi_semaphore_destroy(st->done);
    memset(st, 0, sizeof(*st));
    return 0;
}

/* ======================================================================
 *  Bus configuration
 * ====================================================================== */

dmod_dmsdio_port_api_declaration(1.0, int, _set_power, ( dmsdio_instance_t instance, bool on ))
{
    if (get_ready_state(instance) == NULL)
    {
        return -ENODEV;
    }
    volatile stm32_sdio_t* regs = get_regs(instance);
    if (on)
    {
        regs->POWER = STM32_SDIO_POWER_ON;
        register_settle();
        regs->CLKCR |= STM32_SDIO_CLKCR_CLKEN;
    }
    else
    {
        regs->CLKCR &= ~STM32_SDIO_CLKCR_CLKEN;
        register_settle();
        regs->POWER = STM32_SDIO_POWER_OFF;
    }
    register_settle();
    return 0;
}

/* SDIO_CK = SDIOCLK / (CLKDIV + 2), or SDIOCLK itself with BYPASS. */
static int compute_divider(uint32_t source_hz, uint32_t max_hz, uint32_t* clkcr_bits, uint32_t* actual_hz)
{
    if (max_hz >= source_hz)
    {
        *clkcr_bits = STM32_SDIO_CLKCR_BYPASS;
        *actual_hz  = source_hz;
        return 0;
    }
    uint32_t divisor = (source_hz + max_hz - 1U) / max_hz;     /* round up: never exceed max_hz */
    divisor = (divisor < 2U) ? 2U : divisor;
    if (divisor - 2U > STM32_SDIO_CLKCR_CLKDIV_Msk)
    {
        return -ERANGE;
    }
    *clkcr_bits = divisor - 2U;
    *actual_hz  = source_hz / divisor;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_clock, ( dmsdio_instance_t instance, uint32_t max_hz, uint32_t* actual_hz ))
{
    stm32_sdio_state_t* st = get_ready_state(instance);
    if (st == NULL || max_hz == 0)
    {
        return (st == NULL) ? -ENODEV : -EINVAL;
    }
    uint32_t source = (uint32_t)dmclk_port_get_domain_frequency(dmclk_domain_sdio);
    uint32_t bits = 0;
    uint32_t actual = 0;
    int ret = (source == 0) ? -EIO : compute_divider(source, max_hz, &bits, &actual);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmsdio_port: cannot derive %u Hz from %u Hz\n", (unsigned)max_hz, (unsigned)source);
        return ret;
    }
    volatile stm32_sdio_t* regs = get_regs(instance);
    uint32_t clkcr = regs->CLKCR & ~(STM32_SDIO_CLKCR_CLKDIV_Msk | STM32_SDIO_CLKCR_BYPASS);
    regs->CLKCR = clkcr | bits;
    register_settle();
    st->clock_hz = actual;
    if (actual_hz != NULL)
    {
        *actual_hz = actual;
    }
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_bus_width, ( dmsdio_instance_t instance, dmsdio_bus_width_t width ))
{
    if (get_ready_state(instance) == NULL)
    {
        return -ENODEV;
    }
    if (width != dmsdio_bus_width_1bit && width != dmsdio_bus_width_4bit)
    {
        return -EINVAL;
    }
    volatile stm32_sdio_t* regs = get_regs(instance);
    uint32_t clkcr = regs->CLKCR & ~STM32_SDIO_CLKCR_WIDBUS_Msk;
    regs->CLKCR = clkcr | ((width == dmsdio_bus_width_4bit) ? STM32_SDIO_CLKCR_WIDBUS_4BIT : 0U);
    register_settle();
    return 0;
}

/* ======================================================================
 *  Interrupt handler
 * ====================================================================== */

/*
 * Move as many words as the FIFO allows in one interrupt: a full RX FIFO
 * (or an empty TX FIFO) is serviced as 32 words, a half-full/half-empty one
 * as 8, and the status is re-sampled until neither applies. This keeps the
 * number of interrupts - whose entry/dispatch cost is what limits PIO
 * throughput - as low as possible.
 */
static uint32_t rx_burst(uint32_t sta)
{
    if (sta & STM32_SDIO_STA_RXFIFOF)
    {
        return STM32_SDIO_FIFO_WORDS;
    }
    return (sta & STM32_SDIO_STA_RXFIFOHF) ? STM32_SDIO_FIFO_HALF_WORDS : 0U;
}

static uint32_t tx_burst(uint32_t sta)
{
    if (sta & STM32_SDIO_STA_TXFIFOE)
    {
        return STM32_SDIO_FIFO_WORDS;
    }
    return (sta & STM32_SDIO_STA_TXFIFOHE) ? STM32_SDIO_FIFO_HALF_WORDS : 0U;
}

static void service_fifo(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, uint32_t sta)
{
    uint32_t* cursor = st->cursor;
    uint32_t left = st->words_left;
    uint32_t burst = st->data_read ? rx_burst(sta) : tx_burst(sta);
    while (burst != 0U && left != 0U)
    {
        uint32_t n = (burst < left) ? burst : left;
        left -= n;
        if (st->data_read)
        {
            for (; n != 0U; n--) { *cursor++ = regs->FIFO; }
        }
        else
        {
            for (; n != 0U; n--) { regs->FIFO = *cursor++; }
        }
        sta = regs->STA;
        burst = st->data_read ? rx_burst(sta) : tx_burst(sta);
    }
    st->cursor = cursor;
    st->words_left = left;
    if (!st->data_read && left == 0U)
    {
        regs->MASK &= ~STM32_SDIO_STA_TXFIFOHE;
    }
}

static void finish_data(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, uint32_t sta)
{
    while (st->data_read && st->words_left > 0 && (regs->STA & STM32_SDIO_STA_RXDAVL))
    {
        *st->cursor++ = regs->FIFO;
        st->words_left--;
    }
    regs->MASK &= ~(STM32_SDIO_STA_RXFIFOHF | STM32_SDIO_STA_TXFIFOHE |
                    STM32_SDIO_STA_DATAEND | data_error_flags());
    regs->ICR = STM32_SDIO_STA_DATAEND | STM32_SDIO_STA_DBCKEND | data_error_flags();
    st->data_status = sta;
    st->data_busy = false;
    dmosi_semaphore_post(st->done, 1);
}

void stm32_sdio_irq_handler(dmsdio_instance_t instance)
{
    stm32_sdio_state_t* st = get_ready_state(instance);
    if (st == NULL)
    {
        return;
    }
    volatile stm32_sdio_t* regs = get_regs(instance);
    uint32_t sta = regs->STA;

    if (st->cmd_busy && (sta & STM32_SDIO_CMD_FLAGS))
    {
        regs->MASK &= ~STM32_SDIO_CMD_FLAGS;
        regs->ICR = STM32_SDIO_CMD_FLAGS;
        st->cmd_status = sta;
        st->cmd_busy = false;
        dmosi_semaphore_post(st->done, 1);
    }
    if (st->data_busy)
    {
        service_fifo(regs, st, sta);
        if (sta & (STM32_SDIO_STA_DATAEND | data_error_flags()))
        {
            finish_data(regs, st, sta);
        }
    }
}

/* ======================================================================
 *  Transport
 * ====================================================================== */

static uint32_t block_size_code(uint32_t block_size)
{
    uint32_t code = 0;
    while ((1U << code) < block_size && code < 14U)
    {
        code++;
    }
    return ((1U << code) == block_size) ? code : UINT32_MAX;
}

/* Program DTIMER/DLEN/DCTRL. For reads the DPSM and FIFO interrupts start now. */
static dmsdio_status_t arm_data(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, const dmsdio_data_t* data)
{
    uint32_t code = block_size_code(data->block_size);
    uint64_t length = (uint64_t)data->block_size * data->block_count;
    if (data->buffer == NULL || data->block_count == 0 || code == UINT32_MAX ||
        length > STM32_SDIO_MAX_DATA_LENGTH || ((uintptr_t)data->buffer % 4U) != 0 ||
        (data->block_size % 4U) != 0)
    {
        return dmsdio_status_invalid;
    }
    uint64_t ticks = (uint64_t)data->timeout_ms * st->clock_hz / 1000U;
    st->data_read  = (data->direction == dmsdio_direction_read);
    st->cursor     = (uint32_t*)data->buffer;
    st->words_left = (uint32_t)(length / 4U);
    st->data_busy  = true;

    regs->DTIMER = (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
    regs->DLEN   = (uint32_t)length;
    regs->DCTRL  = STM32_SDIO_DCTRL_DTEN | (code << STM32_SDIO_DCTRL_DBLOCKSIZE_Pos) |
                   (st->data_read ? STM32_SDIO_DCTRL_DTDIR_READ : 0U);
    if (st->data_read)
    {
        mask_enable(regs, STM32_SDIO_STA_RXFIFOHF | STM32_SDIO_STA_DATAEND | data_error_flags());
    }
    return dmsdio_status_ok;
}

static uint32_t command_register(const dmsdio_command_t* command)
{
    uint32_t value = (command->index & STM32_SDIO_CMD_INDEX_Msk) | STM32_SDIO_CMD_CPSMEN;
    switch (command->response)
    {
        case dmsdio_response_none:  return value;
        case dmsdio_response_long:  return value | STM32_SDIO_CMD_WAITRESP_LONG;
        default:                    return value | STM32_SDIO_CMD_WAITRESP_SHORT;
    }
}

static dmsdio_status_t command_result(const dmsdio_command_t* command, uint32_t sta)
{
    if (sta & STM32_SDIO_STA_CTIMEOUT)
    {
        return dmsdio_status_cmd_timeout;
    }
    /* R3 carries no valid CRC: the CPSM always flags CCRCFAIL for it. */
    if ((sta & STM32_SDIO_STA_CCRCFAIL) && command->response != dmsdio_response_short_no_crc)
    {
        return dmsdio_status_cmd_crc;
    }
    return dmsdio_status_ok;
}

static dmsdio_status_t send_command(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st,
                                    const dmsdio_command_t* command, dmsdio_response_t* response)
{
    uint32_t done = (command->response == dmsdio_response_none)
                  ? STM32_SDIO_STA_CMDSENT
                  : (STM32_SDIO_STA_CMDREND | STM32_SDIO_STA_CCRCFAIL | STM32_SDIO_STA_CTIMEOUT);
    regs->ICR = STM32_SDIO_CMD_FLAGS;
    st->cmd_busy = true;
    mask_enable(regs, done);
    regs->ARG = command->argument;
    regs->CMD = command_register(command);
    if (!wait_phase(st, &st->cmd_busy, STM32_CMD_TIMEOUT_MS))
    {
        return dmsdio_status_cmd_timeout;
    }
    dmsdio_status_t result = command_result(command, st->cmd_status);
    if (result == dmsdio_status_ok && response != NULL)
    {
        response->index = (uint8_t)(regs->RESPCMD & STM32_SDIO_CMD_INDEX_Msk);
        for (unsigned i = 0; i < 4; i++)
        {
            response->words[i] = regs->RESP[i];
        }
    }
    return result;
}

static dmsdio_status_t data_result(uint32_t sta)
{
    if (sta & STM32_SDIO_STA_DTIMEOUT)
    {
        return dmsdio_status_data_timeout;
    }
    if (sta & (STM32_SDIO_STA_DCRCFAIL | stm32_sdio_family_error_flags))
    {
        return dmsdio_status_data_crc;
    }
    if (sta & (STM32_SDIO_STA_TXUNDERR | STM32_SDIO_STA_RXOVERR))
    {
        return dmsdio_status_overrun;
    }
    return dmsdio_status_ok;
}

static dmsdio_status_t run_data(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, const dmsdio_data_t* data)
{
    if (!st->data_read)
    {
        mask_enable(regs, STM32_SDIO_STA_TXFIFOHE | STM32_SDIO_STA_DATAEND | data_error_flags());
    }
    uint64_t timeout = (uint64_t)data->timeout_ms * data->block_count + STM32_DATA_TIMEOUT_SLACK_MS;
    if (!wait_phase(st, &st->data_busy, (timeout > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout))
    {
        return dmsdio_status_data_timeout;
    }
    if (st->words_left != 0 && data_result(st->data_status) == dmsdio_status_ok)
    {
        return dmsdio_status_overrun;       /* DATAEND without every word moved */
    }
    return data_result(st->data_status);
}

dmod_dmsdio_port_api_declaration(1.0, dmsdio_status_t, _execute,
    ( dmsdio_instance_t instance, const dmsdio_command_t* command,
      const dmsdio_data_t* data, dmsdio_response_t* response ))
{
    stm32_sdio_state_t* st = get_ready_state(instance);
    if (st == NULL || command == NULL || command->index > STM32_SDIO_CMD_INDEX_Msk)
    {
        return dmsdio_status_invalid;
    }
    volatile stm32_sdio_t* regs = get_regs(instance);
    drain_semaphore(st);
    dmsdio_status_t status = (data != NULL) ? arm_data(regs, st, data) : dmsdio_status_ok;
    if (status == dmsdio_status_ok)
    {
        status = send_command(regs, st, command, response);
    }
    if (status == dmsdio_status_ok && data != NULL)
    {
        status = run_data(regs, st, data);
    }
    if (status != dmsdio_status_ok)
    {
        dmsdio_port_abort(instance);
    }
    return status;
}

dmod_dmsdio_port_api_declaration(1.0, void, _abort, ( dmsdio_instance_t instance ))
{
    stm32_sdio_state_t* st = get_ready_state(instance);
    if (st == NULL)
    {
        return;
    }
    volatile stm32_sdio_t* regs = get_regs(instance);
    regs->MASK  = 0;
    regs->CMD   = 0;                    /* stop the CPSM */
    regs->DCTRL = 0;                    /* stop the DPSM */
    register_settle();
    for (uint32_t i = 0; i < STM32_SDIO_FIFO_WORDS && (regs->STA & STM32_SDIO_STA_RXDAVL); i++)
    {
        (void)regs->FIFO;
    }
    regs->ICR = static_flags();
    st->cmd_busy   = false;
    st->data_busy  = false;
    st->words_left = 0;
    drain_semaphore(st);
}
