#define DMOD_ENABLE_REGISTRATION    ON
#include "dmsdio_port.h"
#include "dmod.h"
#include "dmosi.h"
#include "dmheap.h"
#include "esp32s3_sdmmc.h"
#include <errno.h>
#include <string.h>

/*
 * dmsdio_port for the ESP32-S3: the SD/MMC host controller (SDHOST), slot 0 = instance 1. Protocol-free primitives only.
 *
 * The command goes out through the CMD register and its end is the interrupt (CMD_DONE, or the error flags); the data of a block transfer
 * goes by the internal DMA of the host (IDMAC): a chain of descriptors, up to 4096 bytes each, in the internal RAM, on the buffer that
 * the core hands over - which has to be in the internal RAM too (_buffer_is_direct; the PSRAM is not reached by the IDMAC, the core bounces
 * those buffers through the one of _buffer_alloc). The transfer ends with DATA_OVER and the interrupt of the IDMAC. No interrupt is a
 * timeout: every wait is bounded (the command by CMD_TIMEOUT_MS, the data by the time of the core per block).
 *
 * The bus signals go to the pads through the GPIO matrix (dmsdio_port_set_pins; the keys clk_pin, cmd_pin, d0_pin.. of the core). Only
 * the 1-bit bus is used when no d1_pin..d3_pin is given. The card detect is not wired to the host (the core polls for the card); the
 * write protect is not either.
 *
 * The clock of the card is the 160 MHz of the PLL divided by the host divider (2..16) and the card divider (2*n, or none): 400 kHz for
 * the identification, up to 40 MHz.
 */

#define CMD_TIMEOUT_MS              100U
#define BUSY_TIMEOUT_MS             3000U
#define RESET_GUARD                 100000U
#define MAX_CLOCK_HZ                40000000UL
#define DESC_COUNT                  33U                 /* 128 blocks of 512 bytes and one for a start that is not aligned */
#define DMA_HEAP_NAME               "dma"
#define DESC_ALIGNMENT              16U
#define BUFFER_ALIGNMENT            DMSDIO_TRANSFER_ALIGNMENT

/* The internal RAM that the IDMAC reaches (DRAM of the data bus) */
#define DRAM_START                  0x3FC88000UL
#define DRAM_END                    0x3FCF0000UL

#define INT_CMD_ERRORS              (SDMMC_INT_RTO | SDMMC_INT_RCRC | SDMMC_INT_RESP_ERR | SDMMC_INT_HLE)
#define INT_DATA_ERRORS             (SDMMC_INT_DCRC | SDMMC_INT_DRTO | SDMMC_INT_HTO | SDMMC_INT_FRUN | SDMMC_INT_SBE | SDMMC_INT_EBE)
#define IDINT_ERRORS                (SDMMC_IDINT_FBE | SDMMC_IDINT_DU)

typedef struct
{
    bool            initialised;
    int             clk, cmd, d0, d1, d2, d3;
    bool            wide_pins;                  /* d1..d3 given: a 4-bit bus is possible */
    bool            wide_active;                /* the data lines 1..3 are connected to the host (the 4-bit bus is set) */
    bool            routed;
    uint32_t        clock_khz;
    dmosi_semaphore_t done;
    volatile uint32_t rint;                     /* flags of RINTSTS gathered by the interrupt */
    volatile uint32_t idsts;                    /* the same of IDSTS */
    uint32_t       *desc;                       /* DESC_COUNT * 4 words, internal RAM */
} sdmmc_t;

static sdmmc_t host = { .clk = -1, .cmd = -1, .d0 = -1, .d1 = -1, .d2 = -1, .d3 = -1 };

static inline volatile uint32_t *reg(uintptr_t address)
{
    return (volatile uint32_t *)address;
}

static inline uint32_t intenable_read(void)
{
    uint32_t v;
    __asm__ __volatile__("rsr.intenable %0" : "=a"(v));
    return v;
}

static inline void intenable_write(uint32_t v)
{
    __asm__ __volatile__("wsr.intenable %0; rsync" ::"a"(v) : "memory");
}

static bool instance_valid(dmsdio_instance_t instance)
{
    return instance == 1U;
}

/* ---- Pads ---- */

/* GPIO0-21 and GPIO38-48; GPIO19/20 (USB) and GPIO26-37 (the flash and the PSRAM of the module) are not for the user */
static bool pad_valid(int pad)
{
    return pad >= 0 && (pad <= 18 || pad == 21 || (pad >= 38 && pad <= 48));
}

static void pad_to_matrix(int pad)
{
    volatile uint32_t *mux = reg(ESP32S3_IO_MUX_REG((uint32_t)pad));
    *mux = (*mux & ~ESP32S3_IO_MUX_MCU_SEL_M) | (ESP32S3_IO_MUX_FUNC_GPIO << ESP32S3_IO_MUX_MCU_SEL_S) | ESP32S3_IO_MUX_IE;
}

static void route_output(int pad, uint32_t signal)
{
    pad_to_matrix(pad);
    *reg(ESP32S3_GPIO_FUNC_OUT_SEL_REG((uint32_t)pad)) = signal;                /* the output enable is the signal's own */
}

static void route_input(int pad, uint32_t signal)
{
    pad_to_matrix(pad);
    *reg(ESP32S3_GPIO_FUNC_IN_SEL_REG(signal)) = ESP32S3_GPIO_IN_SEL_ROUTED | (uint32_t)pad;
}

static void unroute(int pad, uint32_t signal, bool input)
{
    if (pad < 0)
        return;
    *reg(ESP32S3_GPIO_FUNC_OUT_SEL_REG((uint32_t)pad)) = 256UL;                 /* the GPIO output register again */
    if (input)
        *reg(ESP32S3_GPIO_FUNC_IN_SEL_REG(signal)) = ESP32S3_GPIO_IN_SEL_ROUTED | ESP32S3_GPIO_CONST_ZERO_INPUT;
}

/* A pure output: the pad is an output whatever the signal says (the output enable of the pad is the GPIO register, ESP-IDF does the same) */
static void route_driven_output(int pad, uint32_t signal)
{
    pad_to_matrix(pad);
    *reg(ESP32S3_GPIO_FUNC_OUT_SEL_REG((uint32_t)pad)) = signal | ESP32S3_GPIO_OEN_SEL_REGISTER;
    *reg(ESP32S3_GPIO_ENABLE_W1TS_REG) = 1UL << pad;
}

static void route_bidirectional(int pad, uint32_t signal)
{
    route_output(pad, signal);
    route_input(pad, signal);
    *reg(ESP32S3_IO_MUX_REG((uint32_t)pad)) |= ESP32S3_IO_MUX_WPU;              /* the lines of an SD bus idle high (the weak pull-up helps a bus without its own) */
}

/* D3 is the CS of the SPI mode of a card: low at the first command it would take the card there. Held high as a plain output until a 4-bit
 * bus uses the line for data (the way ESP-IDF does it too) */
static void d3_hold_high(void)
{
    if (host.d3 < 0)
        return;
    pad_to_matrix(host.d3);
    *reg(ESP32S3_GPIO_FUNC_OUT_SEL_REG((uint32_t)host.d3)) = ESP32S3_GPIO_SIMPLE_OUT;
    *reg(ESP32S3_GPIO_OUT_W1TS_REG) = 1UL << host.d3;
    *reg(ESP32S3_GPIO_ENABLE_W1TS_REG) = 1UL << host.d3;
}

static void wide_bus(bool on)
{
    if (!host.wide_pins)
        return;
    if (on)
    {
        route_bidirectional(host.d1, ESP32S3_SDHOST_CDATA_10 + 1UL);
        route_bidirectional(host.d2, ESP32S3_SDHOST_CDATA_10 + 2UL);
        route_bidirectional(host.d3, ESP32S3_SDHOST_CDATA_10 + 3UL);
    }
    else
    {
        unroute(host.d1, ESP32S3_SDHOST_CDATA_10 + 1UL, true);
        unroute(host.d2, ESP32S3_SDHOST_CDATA_10 + 2UL, true);
        unroute(host.d3, ESP32S3_SDHOST_CDATA_10 + 3UL, true);
        d3_hold_high();
    }
    host.wide_active = on;
}

static void unroute_pads(void)
{
    if (!host.routed)
        return;
    if (host.clk >= 0)
        *reg(ESP32S3_GPIO_ENABLE_W1TC_REG) = 1UL << host.clk;
    unroute(host.clk, 0, false);
    unroute(host.cmd, ESP32S3_SDHOST_CCMD_1, true);
    unroute(host.d0, ESP32S3_SDHOST_CDATA_10, true);
    unroute(host.d1, ESP32S3_SDHOST_CDATA_10 + 1UL, true);
    unroute(host.d2, ESP32S3_SDHOST_CDATA_10 + 2UL, true);
    if (host.d3 >= 0)
        *reg(ESP32S3_GPIO_ENABLE_W1TC_REG) = 1UL << host.d3;
    unroute(host.d3, ESP32S3_SDHOST_CDATA_10 + 3UL, true);
    host.routed = false;
    host.wide_pins = false;
    host.wide_active = false;
}

/* ---- The controller ---- */

static void regs_settle(void)
{
    __asm__ __volatile__("memw" ::: "memory");
}

static bool wait_bits_clear(uint32_t offset, uint32_t bits)
{
    uint32_t guard = RESET_GUARD;
    while ((SDMMC_REG(offset) & bits) != 0U && --guard != 0U) { }
    return guard != 0U;
}

/* The reset of the controller, the FIFO and the DMA: the bits clear themselves */
static bool controller_reset(uint32_t bits)
{
    SDMMC_REG(SDMMC_CTRL) |= bits;
    return wait_bits_clear(SDMMC_CTRL, bits);
}

/* The clock registers go to the card interface through a command that is not sent to the card */
static bool clock_update(void)
{
    for (unsigned attempt = 0; attempt < 8U; attempt++)
    {
        uint32_t guard = RESET_GUARD;
        while ((SDMMC_REG(SDMMC_CMD) & SDMMC_CMD_START) != 0U && --guard != 0U) { }
        if (guard == 0U)
            return false;
        SDMMC_REG(SDMMC_CMDARG) = 0;
        SDMMC_REG(SDMMC_RINTSTS) = SDMMC_INT_HLE;
        SDMMC_REG(SDMMC_CMD) = SDMMC_CMD_UPDATE_CLOCK | SDMMC_CMD_WAIT_COMPLETE | SDMMC_CMD_USE_HOLD | SDMMC_CMD_START;
        guard = RESET_GUARD;
        while ((SDMMC_REG(SDMMC_CMD) & SDMMC_CMD_START) != 0U && (SDMMC_REG(SDMMC_RINTSTS) & SDMMC_INT_HLE) == 0U && --guard != 0U) { }
        if ((SDMMC_REG(SDMMC_RINTSTS) & SDMMC_INT_HLE) != 0U)
        {
            SDMMC_REG(SDMMC_RINTSTS) = SDMMC_INT_HLE;                      /* the manual: it is fine, again */
            continue;
        }
        return guard != 0U;
    }
    return false;
}

static void card_clock_enable(bool enable)
{
    SDMMC_REG(SDMMC_CLKENA) = enable ? 1UL : 0UL;                           /* cclk_enable[0]; no clock gating when the card is idle */
    (void)clock_update();
}

/* The highest clock not above max_hz: 160 MHz / host divider (2..16) / (2 * card divider (1..255), or 1) */
static bool clock_dividers(uint32_t max_hz, uint32_t *host_div, uint32_t *card_div, uint32_t *actual_hz)
{
    uint32_t best = 0, best_host = 0, best_card = 0;

    if (max_hz > MAX_CLOCK_HZ)
        max_hz = MAX_CLOCK_HZ;
    for (uint32_t h = 2; h <= 16U; h++)
    {
        for (uint32_t c = 0; c <= 255U; c++)
        {
            uint32_t hz = (uint32_t)(SDMMC_CLOCK_SOURCE_HZ / h / (c == 0U ? 1UL : 2UL * c));
            if (hz <= max_hz && hz > best)
            {
                best = hz;
                best_host = h;
                best_card = c;
            }
            if (hz <= max_hz)
                break;                                                      /* a larger c only lowers it */
        }
    }
    if (best == 0U)
        return false;
    *host_div = best_host;
    *card_div = best_card;
    *actual_hz = best;
    return true;
}

static void data_timeout_set(void)
{
    /* The read timeout of the controller: 100 ms of cycles of the card clock (the core bounds each block itself); the response one: 255 */
    uint32_t cycles = 100UL * (host.clock_khz != 0U ? host.clock_khz : 400UL);
    if (cycles > 0xFFFFFFUL)
        cycles = 0xFFFFFFUL;
    SDMMC_REG(SDMMC_TMOUT) = (cycles << 8) | 0xFFUL;
}

/* ---- Interrupt ---- */

static void sdmmc_irq(void)
{
    uint32_t pending = SDMMC_REG(SDMMC_MINTSTS) & 0xFFFFUL;
    uint32_t dma = SDMMC_REG(SDMMC_IDSTS);

    SDMMC_REG(SDMMC_RINTSTS) = pending;
    SDMMC_REG(SDMMC_IDSTS) = dma;
    if (pending != 0U || (dma & 0x3FFUL) != 0U)
    {
        host.rint |= pending;
        host.idsts |= dma & 0x3FFUL;
        if (host.done != NULL)
            (void)dmosi_semaphore_post(host.done, 1);
    }
}

DMOD_IRQ_HANDLER(18)
{
    sdmmc_irq();
}

static void irq_route(bool on)
{
    if (on)
    {
        *reg(ESP32S3_INTMTX_SDMMC_MAP_REG) = ESP32S3_SDMMC_CPU_INTR;
        intenable_write(intenable_read() | (1UL << ESP32S3_SDMMC_CPU_INTR));
    }
    else
    {
        intenable_write(intenable_read() & ~(1UL << ESP32S3_SDMMC_CPU_INTR));
    }
}

static void drain_semaphore(void)
{
    while (dmosi_semaphore_wait(host.done, 1, 0) == 0) { }
}

/* Waits until (rint & rint_mask) != 0 or, with idsts_mask, until both parts are there; the flags are those of the interrupt */
static bool wait_events(uint32_t rint_done, uint32_t idsts_done, uint32_t errors, uint32_t timeout_ms)
{
    uint64_t start = Dmod_GetUptime();
    for (;;)
    {
        uint32_t rint = host.rint, idsts = host.idsts;
        if ((rint & errors) != 0U || (idsts & IDINT_ERRORS) != 0U)
            return true;
        if ((rint & rint_done) == rint_done && (idsts & idsts_done) == idsts_done)
            return true;
        uint64_t elapsed = Dmod_GetUptime() - start;
        if (elapsed >= timeout_ms)
            return false;
        (void)dmosi_semaphore_wait(host.done, 1, (int32_t)(timeout_ms - elapsed));
    }
}

/* ---- Lifecycle ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _host_init, ( dmsdio_instance_t instance ))
{
    if (!instance_valid(instance))
    {
        DMOD_LOG_ERROR("dmsdio_port: no SD/MMC host %u on this chip (only 1)\n", (unsigned)instance);
        return -ENODEV;
    }
    if (host.initialised)
        return -EBUSY;

    host.done = dmosi_semaphore_create(0, 8);
    dmheap_context_t *heap = dmheap_get_context_by_name(DMA_HEAP_NAME);
    host.desc = (heap != NULL) ? dmheap_aligned_alloc(heap, DESC_ALIGNMENT, DESC_COUNT * SDMMC_DESC_BYTES, "dmsdio_port")
                               : Dmod_AlignedMalloc(DESC_COUNT * SDMMC_DESC_BYTES, DESC_ALIGNMENT);
    if (host.done == NULL || host.desc == NULL)
    {
        if (host.done != NULL)
            dmosi_semaphore_destroy(host.done);
        host.done = NULL;
        return -ENOMEM;
    }

    Dmod_EnterCritical();
    *reg(ESP32S3_PERIP_CLK_EN1_REG) |= ESP32S3_SDMMC_PERIP_BIT;
    *reg(ESP32S3_PERIP_RST_EN1_REG) |= ESP32S3_SDMMC_PERIP_BIT;
    *reg(ESP32S3_PERIP_RST_EN1_REG) &= ~ESP32S3_SDMMC_PERIP_BIT;
    Dmod_ExitCritical();

    SDMMC_REG(SDMMC_CLOCK) = SDMMC_CLOCK_VALUE(2UL);                        /* the host clock first: the reset needs it */
    if (!controller_reset(SDMMC_CTRL_CONTROLLER_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET))
    {
        DMOD_LOG_ERROR("dmsdio_port: the SD/MMC host does not leave the reset\n");
        dmosi_semaphore_destroy(host.done);
        host.done = NULL;
        return -EIO;
    }

    /* The card detect is "present" and the write protect "not protected": neither is wired to the host */
    *reg(ESP32S3_GPIO_FUNC_IN_SEL_REG(ESP32S3_SDHOST_CARD_DETECT_N_1)) = ESP32S3_GPIO_IN_SEL_ROUTED | ESP32S3_GPIO_CONST_ZERO_INPUT;
    *reg(ESP32S3_GPIO_FUNC_IN_SEL_REG(ESP32S3_SDHOST_CARD_WRITE_PRT_1)) = ESP32S3_GPIO_IN_SEL_ROUTED | ESP32S3_GPIO_CONST_ZERO_INPUT;

    SDMMC_REG(SDMMC_RINTSTS) = 0xFFFFFFFFUL;
    SDMMC_REG(SDMMC_IDSTS) = 0xFFFFFFFFUL;
    SDMMC_REG(SDMMC_INTMASK) = SDMMC_INT_RESP_ERR | SDMMC_INT_CMD_DONE | SDMMC_INT_DATA_OVER | SDMMC_INT_RCRC | SDMMC_INT_DCRC |
                               SDMMC_INT_RTO | SDMMC_INT_DRTO | SDMMC_INT_HTO | SDMMC_INT_FRUN | SDMMC_INT_HLE | SDMMC_INT_SBE | SDMMC_INT_EBE;
    SDMMC_REG(SDMMC_BMOD) = SDMMC_BMOD_SW_RESET;
    SDMMC_REG(SDMMC_IDINTEN) = SDMMC_IDINT_TI | SDMMC_IDINT_RI | SDMMC_IDINT_FBE | SDMMC_IDINT_DU | SDMMC_IDINT_NIS | SDMMC_IDINT_AIS;
    SDMMC_REG(SDMMC_CTRL) |= SDMMC_CTRL_INT_ENABLE | SDMMC_CTRL_DMA_ENABLE;
    SDMMC_REG(SDMMC_PWREN) = 0;
    SDMMC_REG(SDMMC_CLKENA) = 0;
    SDMMC_REG(SDMMC_CLKSRC) = 0;                                            /* the card 0 uses the divider 0 */
    SDMMC_REG(SDMMC_CTYPE) = 0;
    host.rint = 0;
    host.idsts = 0;
    irq_route(true);
    host.initialised = true;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _host_deinit, ( dmsdio_instance_t instance ))
{
    if (!instance_valid(instance) || !host.initialised)
        return -ENODEV;
    irq_route(false);
    SDMMC_REG(SDMMC_INTMASK) = 0;
    SDMMC_REG(SDMMC_CLKENA) = 0;
    SDMMC_REG(SDMMC_PWREN) = 0;
    (void)controller_reset(SDMMC_CTRL_CONTROLLER_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET);
    unroute_pads();
    Dmod_EnterCritical();
    *reg(ESP32S3_PERIP_CLK_EN1_REG) &= ~ESP32S3_SDMMC_PERIP_BIT;
    Dmod_ExitCritical();
    dmosi_semaphore_destroy(host.done);
    host.done = NULL;
    dmheap_context_t *heap = dmheap_get_context_by_name(DMA_HEAP_NAME);
    if (heap != NULL)
        dmheap_free(heap, host.desc, true);
    else
        Dmod_Free(host.desc);
    host.desc = NULL;
    host.initialised = false;
    host.clock_khz = 0;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_pins,
    ( dmsdio_instance_t instance, int clk_pin, int cmd_pin, int d0_pin, int d1_pin, int d2_pin, int d3_pin ))
{
    if (!instance_valid(instance) || !host.initialised)
        return -ENODEV;
    /* d1_pin and d2_pin make a 4-bit bus possible (with d3_pin); d3_pin alone is the line held high on a 1-bit bus */
    bool wide = d1_pin >= 0 || d2_pin >= 0;
    if (!pad_valid(clk_pin) || !pad_valid(cmd_pin) || !pad_valid(d0_pin))
        return -EINVAL;
    if (d3_pin >= 0 && !pad_valid(d3_pin))
        return -EINVAL;
    if (wide && (!pad_valid(d1_pin) || !pad_valid(d2_pin) || d3_pin < 0))
        return -EINVAL;
    unroute_pads();

    host.clk = clk_pin;
    host.cmd = cmd_pin;
    host.d0 = d0_pin;
    host.d1 = d1_pin;
    host.d2 = d2_pin;
    host.d3 = d3_pin;
    route_driven_output(clk_pin, ESP32S3_SDHOST_CCLK_OUT_1);
    route_bidirectional(cmd_pin, ESP32S3_SDHOST_CCMD_1);
    route_bidirectional(d0_pin, ESP32S3_SDHOST_CDATA_10);
    d3_hold_high();
    host.wide_pins = wide;
    host.wide_active = false;
    host.routed = true;
    return 0;
}

/* ---- Bus configuration ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _set_power, ( dmsdio_instance_t instance, bool on ))
{
    if (!instance_valid(instance) || !host.initialised)
        return -ENODEV;
    if (on)
    {
        SDMMC_REG(SDMMC_PWREN) = 1;
        if (host.clock_khz == 0U)
        {
            uint32_t actual = 0;
            int ret = dmsdio_port_set_clock(instance, 400000UL, &actual);
            if (ret != 0)
                return ret;
        }
        card_clock_enable(true);
    }
    else
    {
        card_clock_enable(false);
        SDMMC_REG(SDMMC_PWREN) = 0;
    }
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_clock, ( dmsdio_instance_t instance, uint32_t max_hz, uint32_t *actual_hz ))
{
    uint32_t host_div, card_div, actual;

    if (!instance_valid(instance) || !host.initialised)
        return -ENODEV;
    if (max_hz == 0U || !clock_dividers(max_hz, &host_div, &card_div, &actual))
        return -EINVAL;

    uint32_t enabled = SDMMC_REG(SDMMC_CLKENA);
    card_clock_enable(false);
    SDMMC_REG(SDMMC_CLKDIV) = card_div;                                      /* the divider of the card 0: div0 */
    SDMMC_REG(SDMMC_CLOCK) = SDMMC_CLOCK_VALUE(host_div);
    if (!clock_update())
        return -EIO;
    host.clock_khz = actual / 1000UL;
    data_timeout_set();
    if ((enabled & 1UL) != 0U)
        card_clock_enable(true);
    if (actual_hz != NULL)
        *actual_hz = actual;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_bus_width, ( dmsdio_instance_t instance, dmsdio_bus_width_t width ))
{
    if (!instance_valid(instance) || !host.initialised)
        return -ENODEV;
    if (width != dmsdio_bus_width_1bit && width != dmsdio_bus_width_4bit)
        return -EINVAL;
    if (width == dmsdio_bus_width_4bit && !host.wide_pins)
        return -ENOTSUP;                                                    /* d1_pin..d3_pin are not given */
    wide_bus(width == dmsdio_bus_width_4bit);                               /* the data lines 1..3 go to the host, or D3 is held high again */
    SDMMC_REG(SDMMC_CTYPE) = (width == dmsdio_bus_width_4bit) ? 1UL : 0UL;
    return 0;
}

/* ---- Transport ---- */

static uint32_t command_word(const dmsdio_command_t *command, bool data, dmsdio_direction_t direction)
{
    uint32_t word = command->index & 0x3FUL;

    if (command->index == 12U)
        word |= SDMMC_CMD_STOP_ABORT;                                       /* the stop of a transfer: does not wait for it */
    else
        word |= SDMMC_CMD_WAIT_COMPLETE;
    if (command->index == 0U)
        word |= SDMMC_CMD_SEND_INIT;                                        /* the 80 clocks of the start of a card */
    switch (command->response)
    {
        case dmsdio_response_none:
            break;
        case dmsdio_response_long:
            word |= SDMMC_CMD_RESPONSE_EXPECT | SDMMC_CMD_RESPONSE_LONG | SDMMC_CMD_CHECK_RESPONSE_CRC;
            break;
        case dmsdio_response_short_no_crc:
            word |= SDMMC_CMD_RESPONSE_EXPECT;
            break;
        default:
            word |= SDMMC_CMD_RESPONSE_EXPECT | SDMMC_CMD_CHECK_RESPONSE_CRC;
            break;
    }
    if (data)
    {
        word |= SDMMC_CMD_DATA_EXPECTED;
        if (direction == dmsdio_direction_write)
            word |= SDMMC_CMD_WRITE;
    }
    return word | SDMMC_CMD_USE_HOLD;
}

static dmsdio_status_t command_status(uint32_t rint)
{
    if ((rint & SDMMC_INT_RTO) != 0U)
        return dmsdio_status_cmd_timeout;
    if ((rint & (SDMMC_INT_RCRC | SDMMC_INT_RESP_ERR)) != 0U)
        return dmsdio_status_cmd_crc;
    if ((rint & SDMMC_INT_HLE) != 0U)
        return dmsdio_status_overrun;
    return dmsdio_status_ok;
}

static dmsdio_status_t data_status(uint32_t rint, uint32_t idsts)
{
    if ((rint & (SDMMC_INT_DCRC | SDMMC_INT_SBE | SDMMC_INT_EBE)) != 0U)
        return dmsdio_status_data_crc;
    if ((rint & (SDMMC_INT_DRTO | SDMMC_INT_HTO)) != 0U)
        return dmsdio_status_data_timeout;
    if ((rint & SDMMC_INT_FRUN) != 0U || (idsts & IDINT_ERRORS) != 0U)
        return dmsdio_status_overrun;
    return dmsdio_status_ok;
}

static bool dram_buffer(const void *buffer, size_t length)
{
    uintptr_t start = (uintptr_t)buffer;
    return start >= DRAM_START && length <= DRAM_END - DRAM_START && start <= DRAM_END - length && (start & 3U) == 0U;
}

/* The descriptors for a buffer, the IDMAC armed: it starts with the command */
static dmsdio_status_t data_arm(const dmsdio_data_t *data)
{
    uint32_t total = data->block_size * data->block_count;
    uint32_t count = (total + SDMMC_DESC_MAX_CHUNK - 1U) / SDMMC_DESC_MAX_CHUNK;

    if (data->buffer == NULL || data->block_size == 0U || data->block_size > 2048U || data->block_count == 0U || total % 4U != 0U ||
        count > DESC_COUNT || !dram_buffer(data->buffer, total))
        return dmsdio_status_invalid;

    uint8_t *ptr = (uint8_t *)data->buffer;
    uint32_t remaining = total;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t chunk = remaining < SDMMC_DESC_MAX_CHUNK ? remaining : SDMMC_DESC_MAX_CHUNK;
        bool last = chunk == remaining;
        uint32_t *d = host.desc + 4U * i;
        d[1] = chunk;
        d[2] = (uint32_t)(uintptr_t)ptr;
        d[3] = last ? 0UL : (uint32_t)(uintptr_t)(host.desc + 4U * (i + 1U));
        d[0] = (i == 0U ? SDMMC_DESC_FIRST : 0UL) | (last ? SDMMC_DESC_LAST : 0UL) | SDMMC_DESC_CHAINED | SDMMC_DESC_OWNED;
        ptr += chunk;
        remaining -= chunk;
    }
    regs_settle();

    SDMMC_REG(SDMMC_BLKSIZ) = data->block_size;
    SDMMC_REG(SDMMC_BYTCNT) = total;
    SDMMC_REG(SDMMC_DBADDR) = (uint32_t)(uintptr_t)host.desc;
    SDMMC_REG(SDMMC_CTRL) |= SDMMC_CTRL_DMA_ENABLE | SDMMC_CTRL_USE_INTERNAL_DMA;
    SDMMC_REG(SDMMC_BMOD) |= SDMMC_BMOD_ENABLE | SDMMC_BMOD_FB;
    SDMMC_REG(SDMMC_PLDMND) = 1;
    return dmsdio_status_ok;
}

dmod_dmsdio_port_api_declaration(1.0, dmsdio_status_t, _execute,
    ( dmsdio_instance_t instance, const dmsdio_command_t *command, const dmsdio_data_t *data, dmsdio_response_t *response ))
{
    if (!instance_valid(instance) || !host.initialised || command == NULL || command->index > 63U)
        return dmsdio_status_invalid;

    /* The previous command has to be taken by the controller */
    uint32_t guard = RESET_GUARD;
    while ((SDMMC_REG(SDMMC_CMD) & SDMMC_CMD_START) != 0U && --guard != 0U) { }
    if (guard == 0U)
        return dmsdio_status_overrun;

    drain_semaphore();
    Dmod_EnterCritical();
    host.rint = 0;
    host.idsts = 0;
    Dmod_ExitCritical();
    SDMMC_REG(SDMMC_RINTSTS) = 0xFFFFFFFFUL;
    SDMMC_REG(SDMMC_IDSTS) = 0xFFFFFFFFUL;

    if (data != NULL)
    {
        dmsdio_status_t armed = data_arm(data);
        if (armed != dmsdio_status_ok)
            return armed;
    }

    SDMMC_REG(SDMMC_CMDARG) = command->argument;
    SDMMC_REG(SDMMC_CMD) = command_word(command, data != NULL, data != NULL ? data->direction : dmsdio_direction_read) | SDMMC_CMD_START;

    dmsdio_status_t status = dmsdio_status_ok;
    if (!wait_events(SDMMC_INT_CMD_DONE, 0, INT_CMD_ERRORS, CMD_TIMEOUT_MS))
        status = dmsdio_status_cmd_timeout;
    else
        status = command_status(host.rint);

    if (status == dmsdio_status_ok && response != NULL)
    {
        memset(response, 0, sizeof(*response));
        response->index = (uint8_t)SDMMC_STATUS_RESPONSE_INDEX(SDMMC_REG(SDMMC_STATUS));
        if (command->response == dmsdio_response_long)
        {
            for (unsigned i = 0; i < 4U; i++)
                response->words[i] = SDMMC_REG(SDMMC_RESP(3U - i));          /* the most significant word first */
        }
        else if (command->response != dmsdio_response_none)
        {
            response->words[0] = SDMMC_REG(SDMMC_RESP(0));
        }
    }

    if (status == dmsdio_status_ok && command->response == dmsdio_response_short_busy)
    {
        uint64_t start = Dmod_GetUptime();
        while ((SDMMC_REG(SDMMC_STATUS) & SDMMC_STATUS_DATA_BUSY) != 0U)
        {
            if (Dmod_GetUptime() - start >= BUSY_TIMEOUT_MS)
            {
                status = dmsdio_status_data_timeout;
                break;
            }
            dmosi_thread_sleep(1);
        }
    }

    if (status == dmsdio_status_ok && data != NULL)
    {
        bool read = data->direction == dmsdio_direction_read;
        uint32_t timeout = data->timeout_ms * data->block_count + CMD_TIMEOUT_MS;
        if (!wait_events(SDMMC_INT_DATA_OVER, read ? SDMMC_IDINT_RI : SDMMC_IDINT_TI, INT_DATA_ERRORS, timeout))
            status = dmsdio_status_data_timeout;
        else
            status = data_status(host.rint, host.idsts);
    }

    if (status != dmsdio_status_ok)
        dmsdio_port_abort(instance);
    return status;
}

dmod_dmsdio_port_api_declaration(1.0, void, _abort, ( dmsdio_instance_t instance ))
{
    if (!instance_valid(instance) || !host.initialised)
        return;
    SDMMC_REG(SDMMC_CTRL) &= ~SDMMC_CTRL_USE_INTERNAL_DMA;
    SDMMC_REG(SDMMC_BMOD) &= ~(SDMMC_BMOD_ENABLE | SDMMC_BMOD_FB);
    (void)controller_reset(SDMMC_CTRL_DMA_RESET | SDMMC_CTRL_FIFO_RESET);
    SDMMC_REG(SDMMC_RINTSTS) = 0xFFFFFFFFUL;
    SDMMC_REG(SDMMC_IDSTS) = 0xFFFFFFFFUL;
}

/* ---- Buffers ---- */

/* Bounce buffers come from dmod-boot's "dma" heap (the internal RAM the IDMAC reaches); a target without that heap uses the regular one */
dmod_dmsdio_port_api_declaration(1.0, void*, _buffer_alloc, ( dmsdio_instance_t instance, size_t size ))
{
    (void)instance;
    dmheap_context_t *heap = dmheap_get_context_by_name(DMA_HEAP_NAME);
    return (heap != NULL) ? dmheap_aligned_alloc(heap, BUFFER_ALIGNMENT, size, "dmsdio_port")
                          : Dmod_AlignedMalloc(size, BUFFER_ALIGNMENT);
}

dmod_dmsdio_port_api_declaration(1.0, void, _buffer_free, ( dmsdio_instance_t instance, void *buffer ))
{
    (void)instance;
    if (buffer == NULL)
        return;
    dmheap_context_t *heap = dmheap_get_context_by_name(DMA_HEAP_NAME);
    if (heap != NULL)
        dmheap_free(heap, buffer, true);
    else
        Dmod_Free(buffer);
}

/* The IDMAC reaches the internal RAM only (not the PSRAM); a buffer has to be word aligned */
dmod_dmsdio_port_api_declaration(1.0, bool, _buffer_is_direct,
    ( dmsdio_instance_t instance, const void *buffer, size_t length, dmsdio_direction_t direction ))
{
    (void)instance;
    (void)direction;
    return buffer != NULL && length % 4U == 0U && dram_buffer(buffer, length);
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    memset(&host, 0, sizeof(host));
    host.clk = host.cmd = host.d0 = host.d1 = host.d2 = host.d3 = -1;
    return 0;
}

int dmod_deinit(void)
{
    if (host.initialised)
        dmsdio_port_host_deinit(1);
    return 0;
}
