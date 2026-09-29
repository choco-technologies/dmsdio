#ifndef STM32F4_REGS_H
#define STM32F4_REGS_H

#include "stm32_common_regs.h"

/**
 * @brief STM32F4-specific memory addresses and parameters
 */

/* Memory base addresses for STM32F4 */
#define STM32F4_FLASH_BASE      0x40023C00U
#define STM32F4_RCC_BASE        0x40023800U

/* STM32F4 clock frequency limits */
#define STM32F4_MAX_SYSCLK      168000000U  /* Maximum system clock for STM32F4 */
#define STM32F4_MAX_HCLK        168000000U  /* Maximum AHB clock */
#define STM32F4_MAX_PCLK1       42000000U   /* Maximum APB1 clock */
#define STM32F4_MAX_PCLK2       84000000U   /* Maximum APB2 clock */

/* PLL parameters for STM32F4 */
#define STM32F4_PLLM_MIN        2U
#define STM32F4_PLLM_MAX        63U
#define STM32F4_PLLN_MIN        50U
#define STM32F4_PLLN_MAX        432U
#define STM32F4_PLLP_MIN        2U
#define STM32F4_PLLP_MAX        8U
#define STM32F4_PLLQ_MIN        2U
#define STM32F4_PLLQ_MAX        15U

/* VCO frequency range for STM32F4 */
#define STM32F4_VCO_MIN         100000000U  /* Minimum VCO frequency */
#define STM32F4_VCO_MAX         432000000U  /* Maximum VCO frequency */
#define STM32F4_PLL_IN_MIN      1000000U    /* Minimum PLL input frequency */
#define STM32F4_PLL_IN_MAX      2000000U    /* Maximum PLL input frequency */

/* Flash latency settings for STM32F4 (depends on voltage range and frequency) */
/* These are for voltage range 2.7V-3.6V */
static const struct {
    uint32_t max_freq;
    uint32_t latency;
} stm32f4_flash_latency[] = {
    {30000000U, 0U},
    {60000000U, 1U},
    {90000000U, 2U},
    {120000000U, 3U},
    {150000000U, 4U},
    {168000000U, 5U},
};

#define STM32F4_FLASH_LATENCY_COUNT (sizeof(stm32f4_flash_latency) / sizeof(stm32f4_flash_latency[0]))

#endif // STM32F4_REGS_H
