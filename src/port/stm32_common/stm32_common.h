#ifndef STM32_COMMON_H
#define STM32_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmsdio_port.h"
#include "dmosi.h"
#include "dmdma.h"

/**
 * @brief STM32 SDIO (F4, RM0090 section 31) / SDMMC (F7, RM0385 section 35)
 *        register layout.
 *
 * Both families implement the same host IP revision: identical register
 * offsets and identical bit positions for every field this port uses. The
 * differences are limited to which instances exist (base address, RCC bit,
 * IRQ number - see stm32_sdio_instances[] in each family's port.c) and to
 * STBITERR (STA/ICR/MASK bit 9), which only exists on F4
 * (stm32_sdio_family_error_flags).
 */
typedef struct
{
    volatile uint32_t POWER;        /**< 0x00 Power control */
    volatile uint32_t CLKCR;        /**< 0x04 Clock control */
    volatile uint32_t ARG;          /**< 0x08 Argument */
    volatile uint32_t CMD;          /**< 0x0C Command */
    volatile uint32_t RESPCMD;      /**< 0x10 Command response index */
    volatile uint32_t RESP[4];      /**< 0x14-0x20 Response 1..4 */
    volatile uint32_t DTIMER;       /**< 0x24 Data timer */
    volatile uint32_t DLEN;         /**< 0x28 Data length */
    volatile uint32_t DCTRL;        /**< 0x2C Data control */
    volatile uint32_t DCOUNT;       /**< 0x30 Data counter */
    volatile uint32_t STA;          /**< 0x34 Status */
    volatile uint32_t ICR;          /**< 0x38 Interrupt clear */
    volatile uint32_t MASK;         /**< 0x3C Interrupt mask */
    volatile uint32_t RESERVED0[2]; /**< 0x40-0x44 */
    volatile uint32_t FIFOCNT;      /**< 0x48 FIFO counter */
    volatile uint32_t RESERVED1[13];/**< 0x4C-0x7C */
    volatile uint32_t FIFO;         /**< 0x80 Data FIFO */
} stm32_sdio_t;

/* POWER */
#define STM32_SDIO_POWER_OFF            0x0U
#define STM32_SDIO_POWER_ON             0x3U

/* CLKCR */
#define STM32_SDIO_CLKCR_CLKDIV_Msk     0xFFU
#define STM32_SDIO_CLKCR_CLKEN          (1U << 8)
#define STM32_SDIO_CLKCR_BYPASS         (1U << 10)
#define STM32_SDIO_CLKCR_WIDBUS_Msk     (3U << 11)
#define STM32_SDIO_CLKCR_WIDBUS_4BIT    (1U << 11)

/* CMD */
#define STM32_SDIO_CMD_INDEX_Msk        0x3FU
#define STM32_SDIO_CMD_WAITRESP_SHORT   (1U << 6)
#define STM32_SDIO_CMD_WAITRESP_LONG    (3U << 6)
#define STM32_SDIO_CMD_CPSMEN           (1U << 10)

/* DCTRL */
#define STM32_SDIO_DCTRL_DTEN           (1U << 0)
#define STM32_SDIO_DCTRL_DTDIR_READ     (1U << 1)
#define STM32_SDIO_DCTRL_DMAEN          (1U << 3)
#define STM32_SDIO_DCTRL_DBLOCKSIZE_Pos 4U

/* STA / ICR / MASK */
#define STM32_SDIO_STA_CCRCFAIL         (1U << 0)
#define STM32_SDIO_STA_DCRCFAIL         (1U << 1)
#define STM32_SDIO_STA_CTIMEOUT         (1U << 2)
#define STM32_SDIO_STA_DTIMEOUT         (1U << 3)
#define STM32_SDIO_STA_TXUNDERR         (1U << 4)
#define STM32_SDIO_STA_RXOVERR          (1U << 5)
#define STM32_SDIO_STA_CMDREND          (1U << 6)
#define STM32_SDIO_STA_CMDSENT          (1U << 7)
#define STM32_SDIO_STA_DATAEND          (1U << 8)
#define STM32_SDIO_STA_STBITERR         (1U << 9)   /**< F4 only */
#define STM32_SDIO_STA_DBCKEND          (1U << 10)
#define STM32_SDIO_STA_CMDACT           (1U << 11)
#define STM32_SDIO_STA_TXFIFOHE         (1U << 14)
#define STM32_SDIO_STA_RXFIFOHF         (1U << 15)
#define STM32_SDIO_STA_TXFIFOF          (1U << 16)
#define STM32_SDIO_STA_RXFIFOF          (1U << 17)
#define STM32_SDIO_STA_TXFIFOE          (1U << 18)
#define STM32_SDIO_STA_RXDAVL           (1U << 21)

#define STM32_SDIO_CMD_FLAGS    (STM32_SDIO_STA_CCRCFAIL | STM32_SDIO_STA_CTIMEOUT | \
                                 STM32_SDIO_STA_CMDREND | STM32_SDIO_STA_CMDSENT)
#define STM32_SDIO_DATA_ERRORS  (STM32_SDIO_STA_DCRCFAIL | STM32_SDIO_STA_DTIMEOUT | \
                                 STM32_SDIO_STA_TXUNDERR | STM32_SDIO_STA_RXOVERR)
#define STM32_SDIO_STATIC_FLAGS (STM32_SDIO_CMD_FLAGS | STM32_SDIO_DATA_ERRORS | \
                                 STM32_SDIO_STA_DATAEND | STM32_SDIO_STA_DBCKEND)

#define STM32_SDIO_FIFO_WORDS           32U
#define STM32_SDIO_FIFO_BYTES           (STM32_SDIO_FIFO_WORDS * 4U)
/* DMA buffers: a 32-byte D-cache line (Cortex-M7) - which also keeps every
 * 16-byte INC4 word burst inside a 1 KB boundary. */
#define STM32_SDIO_DMA_ALIGNMENT        32U
#define STM32_SDIO_MAX_DATA_LENGTH      0x01FFFFFFU
#define STM32_SDIO_MAX_INSTANCES        2U

/** RCC registers used by this port (same offsets on F4 and F7). */
#define STM32_RCC_BASE          0x40023800UL
#define STM32_RCC_APB2RSTR      (*(volatile uint32_t *)(STM32_RCC_BASE + 0x24UL))
#define STM32_RCC_APB2ENR       (*(volatile uint32_t *)(STM32_RCC_BASE + 0x44UL))

/**
 * @brief Per-instance hardware description, provided by each family's port.c.
 */
typedef struct
{
    uint32_t    base;           /**< Peripheral base address */
    uint32_t    apb2_bit;       /**< Bit in RCC_APB2ENR / RCC_APB2RSTR */
    uint32_t    irqn;           /**< NVIC IRQ number */
    uint8_t     dma_controller; /**< dmdma controller (1 = DMA2) */
    uint8_t     dma_streams[2]; /**< Preferred and alternate stream */
    uint8_t     dma_channel;    /**< DMA request channel (CHSEL) */
} stm32_sdio_instance_desc_t;

/** Instances of this family, indexed by (instance - 1). Defined in <family>/port.c. */
extern const stm32_sdio_instance_desc_t stm32_sdio_instances[];
extern const uint8_t stm32_sdio_instance_count;

/** Family-only data error flags (STBITERR on F4, 0 on F7). Defined in <family>/port.c. */
extern const uint32_t stm32_sdio_family_error_flags;

/** Whether DMA2 can reach [address, address + length) - e.g. not F4 CCM RAM. Defined in <family>/port.c. */
bool stm32_sdio_family_dma_reachable(const void* address, size_t length);

/** Whether the core has a data cache (Cortex-M7: true, Cortex-M4: false). Defined in <family>/port.c. */
extern const bool stm32_sdio_family_has_dcache;

/**
 * @brief Per-instance runtime state (shared by stm32_common.c and stm32_data.c).
 */
typedef struct
{
    bool                initialized;
    dmosi_semaphore_t   done;           /* posted per finished phase (SDIO IRQ, DMA callback) */
    uint32_t            clock_hz;       /* current SDIO_CK */
    dmdma_lease_t       dma;            /* leased DMA2 stream */
    volatile bool       cmd_busy;
    volatile uint32_t   cmd_status;
    volatile bool       data_busy;      /* DPSM running (until DATAEND or a data error) */
    volatile uint32_t   data_status;
    volatile bool       dma_busy;       /* DMA stream running */
    volatile uint32_t   dma_event;
    bool                data_read;
    bool                data_dma;       /* data phase uses DMA (else: FIFO read at DATAEND) */
    uint32_t            dctrl;          /* DCTRL value of the armed data phase */
    void*               dma_buffer;     /* buffer of the running DMA transfer */
    uint32_t            dma_length;     /* its length in bytes */
    uint32_t*           cursor;         /* FIFO-only reads: destination */
    uint32_t            words_left;
} stm32_sdio_state_t;

/* --- stm32_common.c helpers used by stm32_data.c --- */
volatile stm32_sdio_t* stm32_sdio_regs(dmsdio_instance_t instance);
uint32_t stm32_sdio_data_error_flags(void);
void     stm32_sdio_mask_enable(volatile stm32_sdio_t* regs, uint32_t bits);
bool     stm32_sdio_wait(stm32_sdio_state_t* st, volatile bool* busy, uint32_t timeout_ms);

/* --- stm32_data.c: data path (DMA for block data, FIFO for small reads) --- */
int             stm32_sdio_data_init(const stm32_sdio_instance_desc_t* desc, stm32_sdio_state_t* st);
void            stm32_sdio_data_deinit(stm32_sdio_state_t* st);
dmsdio_status_t stm32_sdio_data_arm(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st,
                                    const stm32_sdio_instance_desc_t* desc, const dmsdio_data_t* data);
dmsdio_status_t stm32_sdio_data_run(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, const dmsdio_data_t* data);
void            stm32_sdio_data_irq(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st, uint32_t sta);
void            stm32_sdio_data_abort(volatile stm32_sdio_t* regs, stm32_sdio_state_t* st);

/** Reset the shared per-instance state; called from each family's dmod_init()/dmod_deinit(). */
void stm32_sdio_common_init(void);
void stm32_sdio_common_deinit(void);

/** Shared ISR body, called from each family's DMOD_IRQ_HANDLER. */
void stm32_sdio_irq_handler(dmsdio_instance_t instance);

#endif /* STM32_COMMON_H */
