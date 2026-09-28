/* No DMOD_ENABLE_REGISTRATION here - see stm32_common.c. */
#include "dmod.h"
#include "dmosi.h"
#include "dmdma.h"
#include "stm32_common.h"
#include <errno.h>

/*
 * SDIO/SDMMC data path.
 *
 * Block data moves by DMA: a DMA2 stream leased from dmdma (RM0090 table 43 /
 * RM0385 table 27: stream 3 or 6, channel 4) in FIFO mode with a full
 * threshold, 4-beat word bursts on both sides and the SDIO as flow
 * controller (RM0090 31.3.2 "SDIO APB2 interface", RM0385 35.3.2) - the
 * setup the host needs, since it issues burst requests and ends the
 * transfer itself once DLEN bytes moved. The CPU never touches the FIFO, so
 * interrupt latency cannot overrun it.
 *
 * Reads that fit the 32-word FIFO (SCR, SD Status, CMD6 switch status) do
 * not use DMA: the whole transfer is buffered by the FIFO and read out at
 * DATAEND.
 *
 * D-cache (Cortex-M7): whenever SCB->CCR.DC is set, DMA buffers are cleaned
 * before a write, cleaned+invalidated before a read (so no dirty line can be
 * evicted over the incoming data) and invalidated again after it (lines the
 * CPU speculatively refilled meanwhile). DMA buffers are therefore required
 * to start on and span whole 32-byte cache lines; with the cache disabled
 * (or on Cortex-M4) nothing is done.
 *
 * Ordering: for reads the DMA stream and the DPSM are enabled before the
 * command is sent; for writes the DMA stream is started before the command
 * (it only fills the FIFO) and the DPSM is enabled after a valid response,
 * so no data reaches the card before the command was accepted.
 */

#define STM32_DMA_MAX_ELEMENTS      0xFFFFU
#define STM32_DMA_FINISH_SLACK_MS   50U
#define STM32_DATA_TIMEOUT_SLACK_MS 50U

#define DMA_TERMINAL_EVENTS         (dmdma_event_complete | dmdma_event_error | \
                                     dmdma_event_timeout | dmdma_event_aborted)

/* ---- Cortex-M7 data cache maintenance by address (ARMv7-M SCB) ---- */

#define SCB_CCR             (*(volatile uint32_t *)0xE000ED14UL)
#define SCB_CCR_DC          (1UL << 16)
#define SCB_DCIMVAC         (*(volatile uint32_t *)0xE000EF5CUL)   /* invalidate */
#define SCB_DCCMVAC         (*(volatile uint32_t *)0xE000EF68UL)   /* clean */
#define SCB_DCCIMVAC        (*(volatile uint32_t *)0xE000EF70UL)   /* clean + invalidate */
#define DCACHE_LINE         32U

static bool dcache_enabled(void)
{
    return stm32_sdio_family_has_dcache && (SCB_CCR & SCB_CCR_DC) != 0U;
}

static void dcache_by_address(volatile uint32_t* operation, const void* buffer, uint32_t length)
{
    if (!dcache_enabled() || length == 0U)
    {
        return;
    }
    uintptr_t line = (uintptr_t)buffer & ~(uintptr_t)(DCACHE_LINE - 1U);
    uintptr_t end  = (uintptr_t)buffer + length;
    __asm__ volatile ("dsb" ::: "memory");
    for (; line < end; line += DCACHE_LINE)
    {
        *operation = (uint32_t)line;
    }
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

/* Before the DMA starts: writes need the data in RAM, reads must not have
 * dirty lines that could later be evicted over what the DMA wrote. */
static void dcache_before_dma(stm32_sdio_state_t* st)
{
    dcache_by_address(st->data_read ? &SCB_DCCIMVAC : &SCB_DCCMVAC, st->dma_buffer, st->dma_length);
}

/* After a read: drop lines the CPU may have speculatively refilled. */
static void dcache_after_dma(stm32_sdio_state_t* st)
{
    if (st->data_read)
    {
        dcache_by_address(&SCB_DCIMVAC, st->dma_buffer, st->dma_length);
    }
}

/* dmdma lease callback - interrupt context (or synchronous from _abort). */
static void dma_callback(dmdma_lease_t lease, dmdma_event_t event, void* user_ptr)
{
    (void)lease;
    stm32_sdio_state_t* st = (stm32_sdio_state_t*)user_ptr;
    if (!st->dma_busy || (event & DMA_TERMINAL_EVENTS) == 0)
    {
        return;
    }
    st->dma_event = (uint32_t)event;
    st->dma_busy  = false;
    dmosi_semaphore_post(st->done, 1);
}

int stm32_sdio_data_init(const stm32_sdio_instance_desc_t* desc, stm32_sdio_state_t* st)
{
    for (unsigned i = 0; i < 2 && st->dma == NULL; i++)
    {
        st->dma = dmdma_lease_acquire(desc->dma_controller, desc->dma_streams[i]);
    }
    if (st->dma == NULL)
    {
        DMOD_LOG_ERROR("dmsdio_port: DMA%u streams %u and %u are both unavailable\n",
                       (unsigned)desc->dma_controller + 1U, (unsigned)desc->dma_streams[0],
                       (unsigned)desc->dma_streams[1]);
        return -EBUSY;
    }
    if (dmdma_lease_set_callback(st->dma, dma_callback, st) != 0)
    {
        stm32_sdio_data_deinit(st);
        return -EIO;
    }
    return 0;
}

void stm32_sdio_data_deinit(stm32_sdio_state_t* st)
{
    if (st->dma != NULL)
    {
        dmdma_lease_release(st->dma);
        st->dma = NULL;
    }
}

static uint32_t block_size_code(uint32_t block_size)
{
    uint32_t code = 0;
    while ((1U << code) < block_size && code < 14U)
    {
        code++;
    }
    return ((1U << code) == block_size) ? code : UINT32_MAX;
}

static dmsdio_status_t validate(const dmsdio_data_t* data, uint32_t* code)
{
    uint64_t length = (uint64_t)data->block_size * data->block_count;
    *code = block_size_code(data->block_size);
    if (data->buffer == NULL || data->block_count == 0 || *code == UINT32_MAX ||
        length > STM32_SDIO_MAX_DATA_LENGTH || ((uintptr_t)data->buffer % 4U) != 0 ||
        (data->block_size % 4U) != 0)
    {
        return dmsdio_status_invalid;
    }
    return dmsdio_status_ok;
}

static int start_dma(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st,
                     const stm32_sdio_instance_desc_t* desc, const dmsdio_data_t* data, uint32_t length)
{
    void* fifo = (void*)(uintptr_t)&regs->FIFO;
    uint32_t words = length / 4U;
    dmdma_transfer_config_t cfg = {
        .direction             = st->data_read ? dmdma_direction_peripheral_to_memory
                                               : dmdma_direction_memory_to_peripheral,
        .request               = desc->dma_channel,
        .source_address        = st->data_read ? fifo : data->buffer,
        .destination_address   = st->data_read ? data->buffer : fifo,
        .source_width          = dmdma_data_width_word,
        .destination_width     = dmdma_data_width_word,
        .source_increment      = !st->data_read,
        .destination_increment = st->data_read,
        .circular              = false,
        .priority              = dmdma_priority_very_high,
        /* Ignored by the hardware: the SDIO is the flow controller. */
        .element_count         = (words > STM32_DMA_MAX_ELEMENTS) ? STM32_DMA_MAX_ELEMENTS : words,
        .timeout_ms            = 0,
    };
    dmdma_stream_options_t options = {
        .flow_controller   = dmdma_flow_controller_peripheral,
        .fifo_threshold    = dmdma_fifo_full,
        .source_burst      = dmdma_burst_4,
        .destination_burst = dmdma_burst_4,
    };
    st->dma_buffer = data->buffer;
    st->dma_length = length;
    dcache_before_dma(st);
    st->dma_busy = true;
    int rc = dmdma_lease_start_ex(st->dma, &cfg, &options);
    if (rc != 0)
    {
        st->dma_busy = false;
        DMOD_LOG_ERROR("dmsdio_port: DMA start failed (%d)\n", rc);
    }
    return rc;
}

/* Whole cache lines only, so maintenance never touches neighbouring data. */
static bool dma_capable(const dmsdio_data_t* data, uint32_t length)
{
    return ((uintptr_t)data->buffer % STM32_SDIO_DMA_ALIGNMENT) == 0 &&
           (length % STM32_SDIO_DMA_ALIGNMENT) == 0 &&
           stm32_sdio_family_dma_reachable(data->buffer, length);
}

dmsdio_status_t stm32_sdio_data_arm(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st,
                                    const stm32_sdio_instance_desc_t* desc, const dmsdio_data_t* data)
{
    uint32_t code = 0;
    dmsdio_status_t status = validate(data, &code);
    uint32_t length = data->block_size * data->block_count;
    st->data_read = (data->direction == dmsdio_direction_read);
    st->data_dma  = !(st->data_read && length <= STM32_SDIO_FIFO_BYTES);
    if (status == dmsdio_status_ok && st->data_dma && !dma_capable(data, length))
    {
        DMOD_LOG_ERROR("dmsdio_port: buffer %p not usable for DMA\n", data->buffer);
        status = dmsdio_status_invalid;
    }
    if (status != dmsdio_status_ok)
    {
        return status;
    }
    st->cursor     = (uint32_t*)data->buffer;
    st->words_left = length / 4U;
    st->dctrl      = STM32_SDIO_DCTRL_DTEN | (code << STM32_SDIO_DCTRL_DBLOCKSIZE_Pos) |
                     (st->data_read ? STM32_SDIO_DCTRL_DTDIR_READ : 0U) |
                     (st->data_dma ? STM32_SDIO_DCTRL_DMAEN : 0U);
    uint64_t ticks = (uint64_t)data->timeout_ms * st->clock_hz / 1000U;
    regs->DTIMER = (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
    regs->DLEN   = length;
    if (st->data_dma && start_dma(regs, st, desc, data, length) != 0)
    {
        return dmsdio_status_overrun;
    }
    st->data_busy = true;
    if (st->data_read)
    {
        regs->DCTRL = st->dctrl;
        stm32_sdio_mask_enable(regs, STM32_SDIO_STA_DATAEND | stm32_sdio_data_error_flags());
    }
    return dmsdio_status_ok;
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

/* After DATAEND the DMA flushes its FIFO to memory and reports completion. */
static dmsdio_status_t finish_dma(stm32_sdio_state_t* st)
{
    if (!stm32_sdio_wait(st, &st->dma_busy, STM32_DMA_FINISH_SLACK_MS))
    {
        DMOD_LOG_ERROR("dmsdio_port: DMA did not complete after DATAEND\n");
        return dmsdio_status_overrun;
    }
    dcache_after_dma(st);
    return (st->dma_event & (dmdma_event_error | dmdma_event_timeout)) ? dmsdio_status_overrun
                                                                        : dmsdio_status_ok;
}

dmsdio_status_t stm32_sdio_data_run(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, const dmsdio_data_t* data)
{
    if (!st->data_read)
    {
        regs->DCTRL = st->dctrl;
        stm32_sdio_mask_enable(regs, STM32_SDIO_STA_DATAEND | stm32_sdio_data_error_flags());
    }
    uint64_t timeout = (uint64_t)data->timeout_ms * data->block_count + STM32_DATA_TIMEOUT_SLACK_MS;
    if (!stm32_sdio_wait(st, &st->data_busy, (timeout > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout))
    {
        return dmsdio_status_data_timeout;
    }
    dmsdio_status_t status = data_result(st->data_status);
    if (status == dmsdio_status_ok && st->data_dma)
    {
        status = finish_dma(st);
    }
    if (status == dmsdio_status_ok && !st->data_dma && st->words_left != 0)
    {
        status = dmsdio_status_overrun;     /* DATAEND without every word read */
    }
    return status;
}

/* SDIO interrupt, data part: DATAEND or a data error ends the DPSM phase. */
void stm32_sdio_data_irq(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, uint32_t sta)
{
    if (!st->data_busy || (sta & (STM32_SDIO_STA_DATAEND | stm32_sdio_data_error_flags())) == 0)
    {
        return;
    }
    while (!st->data_dma && st->words_left > 0 && (regs->STA & STM32_SDIO_STA_RXDAVL))
    {
        *st->cursor++ = regs->FIFO;
        st->words_left--;
    }
    regs->MASK &= ~(STM32_SDIO_STA_DATAEND | stm32_sdio_data_error_flags());
    regs->ICR = STM32_SDIO_STA_DATAEND | STM32_SDIO_STA_DBCKEND | stm32_sdio_data_error_flags();
    st->data_status = sta;
    st->data_busy = false;
    dmosi_semaphore_post(st->done, 1);
}

void stm32_sdio_data_abort(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st)
{
    regs->DCTRL = 0;                    /* stop the DPSM */
    if (st->dma != NULL)
    {
        dmdma_lease_abort(st->dma);     /* no-op when idle */
    }
    for (uint32_t i = 0; i < STM32_SDIO_FIFO_WORDS && (regs->STA & STM32_SDIO_STA_RXDAVL); i++)
    {
        (void)regs->FIFO;
    }
    st->data_busy  = false;
    st->dma_busy   = false;
    st->words_left = 0;
}
