#ifndef STM32F7_REGS_H
#define STM32F7_REGS_H

#include "stm32_common_regs.h"

/**
 * @brief STM32F7-specific memory addresses and parameters
 */

/* Memory base addresses for STM32F7 */
#define STM32F7_FLASH_BASE      0x40023C00U
#define STM32F7_RCC_BASE        0x40023800U
#define STM32F7_PWR_BASE        0x40007000U

/* STM32F7 clock frequency limits */
#define STM32F7_MAX_SYSCLK      216000000U  /* Maximum system clock for STM32F7 */
/* Above this HCLK, PWR Over-Drive mode must be enabled (RM0385 "Over-drive
 * switching") or high-frequency accesses (e.g. FMC to external SDRAM)
 * become unreliable - verified on real hardware (STM32F746G-Discovery,
 * HCLK=216MHz without Over-Drive: SDRAM reads corrupted; same board and
 * clock with Over-Drive enabled: byte-exact). ST's own reference examples
 * enable Over-Drive whenever the target is above 180MHz. */
#define STM32F7_MAX_SYSCLK_NO_OVERDRIVE 180000000U
#define STM32F7_MAX_HCLK        216000000U  /* Maximum AHB clock */
#define STM32F7_MAX_PCLK1       54000000U   /* Maximum APB1 clock */
#define STM32F7_MAX_PCLK2       108000000U  /* Maximum APB2 clock */

/* PLL parameters for STM32F7 */
#define STM32F7_PLLM_MIN        2U
#define STM32F7_PLLM_MAX        63U
#define STM32F7_PLLN_MIN        50U
#define STM32F7_PLLN_MAX        432U
#define STM32F7_PLLP_MIN        2U
#define STM32F7_PLLP_MAX        8U
#define STM32F7_PLLQ_MIN        2U
#define STM32F7_PLLQ_MAX        15U

/* VCO frequency range for STM32F7 */
#define STM32F7_VCO_MIN         100000000U  /* Minimum VCO frequency */
#define STM32F7_VCO_MAX         432000000U  /* Maximum VCO frequency */
#define STM32F7_PLL_IN_MIN      1000000U    /* Minimum PLL input frequency */
#define STM32F7_PLL_IN_MAX      2000000U    /* Maximum PLL input frequency */

/* Flash latency settings for STM32F7 (depends on voltage range and frequency) */
/* These are for voltage range 2.7V-3.6V */
static const struct {
    uint32_t max_freq;
    uint32_t latency;
} stm32f7_flash_latency[] = {
    {30000000U, 0U},
    {60000000U, 1U},
    {90000000U, 2U},
    {120000000U, 3U},
    {150000000U, 4U},
    {180000000U, 5U},
    {210000000U, 6U},
    {216000000U, 7U},
};

#define STM32F7_FLASH_LATENCY_COUNT (sizeof(stm32f7_flash_latency) / sizeof(stm32f7_flash_latency[0]))

#endif // STM32F7_REGS_H
