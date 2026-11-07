#ifndef ESP32S3_SDMMC_H
#define ESP32S3_SDMMC_H

#include <stdint.h>

/*
 * The SD/MMC host controller of the ESP32-S3 (a Synopsys DesignWare mobile storage host, "SDHOST"), slot 0 = instance 1.
 * Registers: ESP32-S3 Technical Reference Manual, chapter SD/MMC Host Controller; the names follow soc/sdmmc_reg.h of ESP-IDF.
 *
 * The bus signals go to the pads through the GPIO matrix (dmsdio_port_set_pins): there are no fixed pads.
 */

#define ESP32S3_SDMMC_BASE              0x60028000UL
#define SDMMC_REG(off)                  (*(volatile uint32_t *)(ESP32S3_SDMMC_BASE + (off)))

#define SDMMC_CTRL                      0x000
#define SDMMC_PWREN                     0x004
#define SDMMC_CLKDIV                    0x008
#define SDMMC_CLKSRC                    0x00C
#define SDMMC_CLKENA                    0x010
#define SDMMC_TMOUT                     0x014
#define SDMMC_CTYPE                     0x018
#define SDMMC_BLKSIZ                    0x01C
#define SDMMC_BYTCNT                    0x020
#define SDMMC_INTMASK                   0x024
#define SDMMC_CMDARG                    0x028
#define SDMMC_CMD                       0x02C
#define SDMMC_RESP(i)                   (0x030 + 4 * (i))
#define SDMMC_MINTSTS                   0x040
#define SDMMC_RINTSTS                   0x044
#define SDMMC_STATUS                    0x048
#define SDMMC_CDETECT                   0x050
#define SDMMC_WRTPRT                    0x054
#define SDMMC_BMOD                      0x080
#define SDMMC_PLDMND                    0x084
#define SDMMC_DBADDR                    0x088
#define SDMMC_IDSTS                     0x08C
#define SDMMC_IDINTEN                   0x090
#define SDMMC_CLOCK                     0x800       /* the clock of the host: the divider of 160 MHz, the phases */

/* CTRL */
#define SDMMC_CTRL_CONTROLLER_RESET     (1UL << 0)
#define SDMMC_CTRL_FIFO_RESET           (1UL << 1)
#define SDMMC_CTRL_DMA_RESET            (1UL << 2)
#define SDMMC_CTRL_INT_ENABLE           (1UL << 4)
#define SDMMC_CTRL_DMA_ENABLE           (1UL << 5)
#define SDMMC_CTRL_USE_INTERNAL_DMA     (1UL << 25)

/* CMD */
#define SDMMC_CMD_RESPONSE_EXPECT       (1UL << 6)
#define SDMMC_CMD_RESPONSE_LONG         (1UL << 7)
#define SDMMC_CMD_CHECK_RESPONSE_CRC    (1UL << 8)
#define SDMMC_CMD_DATA_EXPECTED         (1UL << 9)
#define SDMMC_CMD_WRITE                 (1UL << 10)
#define SDMMC_CMD_WAIT_COMPLETE         (1UL << 13)
#define SDMMC_CMD_STOP_ABORT            (1UL << 14)
#define SDMMC_CMD_SEND_INIT             (1UL << 15)
#define SDMMC_CMD_UPDATE_CLOCK          (1UL << 21)
#define SDMMC_CMD_USE_HOLD              (1UL << 29)
#define SDMMC_CMD_START                 (1UL << 31)

/* RINTSTS / INTMASK */
#define SDMMC_INT_RESP_ERR              (1UL << 1)
#define SDMMC_INT_CMD_DONE              (1UL << 2)
#define SDMMC_INT_DATA_OVER             (1UL << 3)
#define SDMMC_INT_RCRC                  (1UL << 6)
#define SDMMC_INT_DCRC                  (1UL << 7)
#define SDMMC_INT_RTO                   (1UL << 8)
#define SDMMC_INT_DRTO                  (1UL << 9)
#define SDMMC_INT_HTO                   (1UL << 10)
#define SDMMC_INT_FRUN                  (1UL << 11)
#define SDMMC_INT_HLE                   (1UL << 12)
#define SDMMC_INT_SBE                   (1UL << 13)
#define SDMMC_INT_EBE                   (1UL << 15)

/* STATUS */
#define SDMMC_STATUS_DATA_BUSY          (1UL << 9)
#define SDMMC_STATUS_RESPONSE_INDEX(s)  (((s) >> 11) & 0x3FU)

/* BMOD */
#define SDMMC_BMOD_SW_RESET             (1UL << 0)
#define SDMMC_BMOD_FB                   (1UL << 1)
#define SDMMC_BMOD_ENABLE               (1UL << 7)

/* IDSTS / IDINTEN: the internal DMA */
#define SDMMC_IDINT_TI                  (1UL << 0)
#define SDMMC_IDINT_RI                  (1UL << 1)
#define SDMMC_IDINT_FBE                 (1UL << 2)
#define SDMMC_IDINT_DU                  (1UL << 4)
#define SDMMC_IDINT_NIS                 (1UL << 8)
#define SDMMC_IDINT_AIS                 (1UL << 9)

/* CLOCK (0x800): phase_dout [2:0], phase_din [5:3], phase_core [8:6], div_h [12:9], div_l [16:13], div_n [20:17], clk_sel [23] (1: PLL 160 MHz) */
#define SDMMC_CLOCK_SOURCE_HZ           160000000UL
#define SDMMC_CLOCK_PHASE_DOUT_90       1UL
#define SDMMC_CLOCK_SEL_PLL             (1UL << 23)
#define SDMMC_CLOCK_VALUE(div)          (SDMMC_CLOCK_PHASE_DOUT_90 | (((div) / 2UL - 1UL) << 9) | (((div) - 1UL) << 13) | (((div) - 1UL) << 17) | SDMMC_CLOCK_SEL_PLL)

/* The internal DMA descriptor chain: 4 words, buffers up to 4096 bytes */
#define SDMMC_DESC_BYTES                16U
#define SDMMC_DESC_MAX_CHUNK            4096U
#define SDMMC_DESC_LAST                 (1UL << 2)
#define SDMMC_DESC_FIRST                (1UL << 3)
#define SDMMC_DESC_CHAINED              (1UL << 4)
#define SDMMC_DESC_OWNED                (1UL << 31)

/* SYSTEM: the clock and the reset of the host (PERIP_CLK_EN1 / PERIP_RST_EN1, bit 7: SDIO_HOST) */
#define ESP32S3_SYSTEM_BASE             0x600C0000UL
#define ESP32S3_PERIP_CLK_EN1_REG       (ESP32S3_SYSTEM_BASE + 0x1CUL)
#define ESP32S3_PERIP_RST_EN1_REG       (ESP32S3_SYSTEM_BASE + 0x24UL)
#define ESP32S3_SDMMC_PERIP_BIT         (1UL << 7)

/* GPIO matrix and IO MUX (soc/gpio_sig_map.h: the signals of the slot 0) */
#define ESP32S3_GPIO_BASE               0x60004000UL
#define ESP32S3_GPIO_OUT_W1TS_REG       (ESP32S3_GPIO_BASE + 0x08UL)
#define ESP32S3_GPIO_OUT_W1TC_REG       (ESP32S3_GPIO_BASE + 0x0CUL)
#define ESP32S3_GPIO_ENABLE_W1TS_REG    (ESP32S3_GPIO_BASE + 0x24UL)
#define ESP32S3_GPIO_ENABLE_W1TC_REG    (ESP32S3_GPIO_BASE + 0x28UL)
#define ESP32S3_GPIO_OEN_SEL_REGISTER   (1UL << 10) /* FUNC_OUT_SEL: the output enable of the pad is the GPIO enable register */
#define ESP32S3_GPIO_SIMPLE_OUT         256UL       /* FUNC_OUT_SEL: the pad follows the GPIO output register */
#define ESP32S3_GPIO_FUNC_IN_SEL_REG(s) (ESP32S3_GPIO_BASE + 0x154UL + 4UL * (s))
#define ESP32S3_GPIO_FUNC_OUT_SEL_REG(p) (ESP32S3_GPIO_BASE + 0x554UL + 4UL * (p))
#define ESP32S3_GPIO_IN_SEL_ROUTED      (1UL << 7)
#define ESP32S3_GPIO_CONST_ZERO_INPUT   0x3CUL
#define ESP32S3_IO_MUX_REG(pad)         (0x60009004UL + 4UL * (pad))
#define ESP32S3_IO_MUX_IE               (1UL << 9)
#define ESP32S3_IO_MUX_WPU              (1UL << 8)
#define ESP32S3_IO_MUX_MCU_SEL_S        12U
#define ESP32S3_IO_MUX_MCU_SEL_M        (7UL << ESP32S3_IO_MUX_MCU_SEL_S)
#define ESP32S3_IO_MUX_FUNC_GPIO        1UL

#define ESP32S3_SDHOST_CCLK_OUT_1       172UL
#define ESP32S3_SDHOST_CCMD_1           178UL       /* the same index for the input and the output */
#define ESP32S3_SDHOST_CDATA_10         180UL       /* ... 183: the data lines 0..3 */
#define ESP32S3_SDHOST_CARD_DETECT_N_1  194UL       /* input: low = a card is there */
#define ESP32S3_SDHOST_CARD_WRITE_PRT_1 196UL       /* input: high = write protected */

/* The interrupt: a source of the interrupt matrix (SDIO_HOST = 30, level) that goes to a level 1 line of the CPU (the others in use: 2 GPIO,
 * 3-5 UARTs and I2C, 7 the flash, 9, 12, 13, 17 the GDMA) */
#define ESP32S3_INTMTX_SDMMC_MAP_REG    (0x600C2000UL + 4UL * 30UL)
#define ESP32S3_SDMMC_CPU_INTR          18U

#endif /* ESP32S3_SDMMC_H */
