#ifndef STM32F7_SDMMC_REGS_H
#define STM32F7_SDMMC_REGS_H

#include <stdint.h>

/**
 * @brief STM32F7 SDMMC1 register map (RM0385, section 32 "SDMMC")
 *
 * Field/bit names match the reference manual exactly so this file is the
 * single place to cross-check against it - port.c never pokes a bit offset
 * directly, only these named macros.
 */

#define STM32F7_SDMMC1_BASE  0x40011000UL
#define STM32F7_RCC_BASE     0x40023800UL

typedef struct
{
    volatile uint32_t POWER;      /* 0x00 */
    volatile uint32_t CLKCR;      /* 0x04 */
    volatile uint32_t ARG;        /* 0x08 */
    volatile uint32_t CMD;        /* 0x0C */
    volatile uint32_t RESPCMD;    /* 0x10 */
    volatile uint32_t RESP1;      /* 0x14 */
    volatile uint32_t RESP2;      /* 0x18 */
    volatile uint32_t RESP3;      /* 0x1C */
    volatile uint32_t RESP4;      /* 0x20 */
    volatile uint32_t DTIMER;     /* 0x24 */
    volatile uint32_t DLEN;       /* 0x28 */
    volatile uint32_t DCTRL;      /* 0x2C */
    volatile uint32_t DCOUNT;     /* 0x30 */
    volatile uint32_t STA;        /* 0x34 */
    volatile uint32_t ICR;        /* 0x38 */
    volatile uint32_t MASK;       /* 0x3C */
    uint32_t           RESERVED0[2]; /* 0x40, 0x44 */
    volatile uint32_t FIFOCNT;    /* 0x48 */
    uint32_t           RESERVED1[13]; /* 0x4C..0x7C */
    volatile uint32_t FIFO;       /* 0x80 */
} STM32F7_SDMMC_TypeDef;

#define STM32F7_SDMMC1  ((STM32F7_SDMMC_TypeDef *)STM32F7_SDMMC1_BASE)

/* --- IDMA (internal DMA), a separate register block starting at +0x50 that
 * overlaps FIFOCNT/reserved space in the struct above by design (RM0385
 * lists them as part of the same peripheral, at fixed absolute offsets -
 * accessed via their own pointers instead of extending the struct, since
 * IDMACTRL/IDMABSIZE (0x50/0x54) sit *before* FIFOCNT's neighbours end and
 * IDMABASE0 (0x58) through the end of FIFO's reserved run). --- */
#define STM32F7_SDMMC1_IDMACTRL   (*(volatile uint32_t *)(STM32F7_SDMMC1_BASE + 0x50UL))
#define STM32F7_SDMMC1_IDMABSIZE  (*(volatile uint32_t *)(STM32F7_SDMMC1_BASE + 0x54UL))
#define STM32F7_SDMMC1_IDMABASE0  (*(volatile uint32_t *)(STM32F7_SDMMC1_BASE + 0x58UL))

/* POWER */
#define SDMMC_POWER_PWRCTRL_OFF   0x0UL
#define SDMMC_POWER_PWRCTRL_ON    0x3UL

/* CLKCR */
#define SDMMC_CLKCR_CLKDIV_Pos    0U
#define SDMMC_CLKCR_CLKDIV_Msk    (0xFFUL << SDMMC_CLKCR_CLKDIV_Pos)
#define SDMMC_CLKCR_CLKEN         (1UL << 8)
#define SDMMC_CLKCR_WIDBUS_1BIT   (0x0UL << 11)
#define SDMMC_CLKCR_WIDBUS_4BIT   (0x1UL << 11)
#define SDMMC_CLKCR_WIDBUS_Msk    (0x3UL << 11)
#define SDMMC_CLKCR_NEGEDGE       (1UL << 13)
#define SDMMC_CLKCR_HWFC_EN       (1UL << 14)

/* CMD */
#define SDMMC_CMD_CMDINDEX_Pos    0U
#define SDMMC_CMD_CMDINDEX_Msk    (0x3FUL << SDMMC_CMD_CMDINDEX_Pos)
#define SDMMC_CMD_WAITRESP_NONE   (0x0UL << 6)
#define SDMMC_CMD_WAITRESP_SHORT  (0x1UL << 6)
#define SDMMC_CMD_WAITRESP_LONG   (0x3UL << 6)
#define SDMMC_CMD_WAITRESP_Msk    (0x3UL << 6)
#define SDMMC_CMD_CPSMEN          (1UL << 10)

/* STA / ICR (same bit positions in both registers) */
#define SDMMC_STA_CCRCFAIL   (1UL << 0)
#define SDMMC_STA_DCRCFAIL   (1UL << 1)
#define SDMMC_STA_CTIMEOUT   (1UL << 2)
#define SDMMC_STA_DTIMEOUT   (1UL << 3)
#define SDMMC_STA_TXUNDERR   (1UL << 4)
#define SDMMC_STA_RXOVERR    (1UL << 5)
#define SDMMC_STA_CMDREND    (1UL << 6)
#define SDMMC_STA_CMDSENT    (1UL << 7)
#define SDMMC_STA_DATAEND    (1UL << 8)
#define SDMMC_STA_DBCKEND    (1UL << 10)
#define SDMMC_STA_RXACT      (1UL << 13)

#define SDMMC_STATIC_FLAGS   0x1FE00FFFUL /* every bit ICR is allowed to clear */

/* DCTRL */
#define SDMMC_DCTRL_DTEN          (1UL << 0)
#define SDMMC_DCTRL_DTDIR_WRITE   (0x0UL << 1) /* controller -> card */
#define SDMMC_DCTRL_DTDIR_READ    (0x1UL << 1) /* card -> controller */
#define SDMMC_DCTRL_DTMODE_BLOCK  (0x0UL << 2)
#define SDMMC_DCTRL_DBLOCKSIZE_Pos 4U

/* IDMACTRL */
#define SDMMC_IDMACTRL_IDMAEN   (1UL << 0)

/* RCC (see stm32_common_regs.h in other modules for the shared base) */
#define STM32F7_RCC_APB2ENR   (*(volatile uint32_t *)(STM32F7_RCC_BASE + 0x44UL))
#define RCC_APB2ENR_SDMMC1EN  (1UL << 11)

#endif /* STM32F7_SDMMC_REGS_H */
