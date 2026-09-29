#ifndef STM32_COMMON_REGS_H
#define STM32_COMMON_REGS_H

#include <stdint.h>

/**
 * @brief Common STM32 RCC (Reset and Clock Control) register definitions
 * 
 * These definitions are common across STM32F4 and STM32F7 families.
 * Memory addresses may differ and should be defined in family-specific headers.
 */

/* RCC Register offsets (relative to RCC base address) */
#define RCC_CR_OFFSET           0x00U   /* Clock control register */
#define RCC_PLLCFGR_OFFSET      0x04U   /* PLL configuration register */
#define RCC_CFGR_OFFSET         0x08U   /* Clock configuration register */
#define RCC_CIR_OFFSET          0x0CU   /* Clock interrupt register */

/* RCC_CR register bits */
#define RCC_CR_HSION            (1U << 0)   /* HSI oscillator ON */
#define RCC_CR_HSIRDY           (1U << 1)   /* HSI oscillator ready */
#define RCC_CR_HSEON            (1U << 16)  /* HSE oscillator ON */
#define RCC_CR_HSERDY           (1U << 17)  /* HSE oscillator ready */
#define RCC_CR_HSEBYP           (1U << 18)  /* HSE oscillator bypass */
#define RCC_CR_PLLON            (1U << 24)  /* Main PLL enable */
#define RCC_CR_PLLRDY           (1U << 25)  /* Main PLL ready */

/* RCC_PLLCFGR register bits and masks */
#define RCC_PLLCFGR_PLLM_Pos    0U
#define RCC_PLLCFGR_PLLM_Msk    (0x3FU << RCC_PLLCFGR_PLLM_Pos)
#define RCC_PLLCFGR_PLLN_Pos    6U
#define RCC_PLLCFGR_PLLN_Msk    (0x1FFU << RCC_PLLCFGR_PLLN_Pos)
#define RCC_PLLCFGR_PLLP_Pos    16U
#define RCC_PLLCFGR_PLLP_Msk    (0x3U << RCC_PLLCFGR_PLLP_Pos)
#define RCC_PLLCFGR_PLLSRC      (1U << 22)  /* PLL source (0=HSI, 1=HSE) */
#define RCC_PLLCFGR_PLLQ_Pos    24U
#define RCC_PLLCFGR_PLLQ_Msk    (0xFU << RCC_PLLCFGR_PLLQ_Pos)

/* RCC_APB1ENR register bits (needed to clock the PWR peripheral before its
 * registers, e.g. for Over-Drive, can be accessed) */
#define RCC_APB1ENR_PWREN       (1U << 28)

/* RCC_CFGR register bits and masks */
#define RCC_CFGR_SW_Pos         0U
#define RCC_CFGR_SW_Msk         (0x3U << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_HSI         0U
#define RCC_CFGR_SW_HSE         1U
#define RCC_CFGR_SW_PLL         2U

#define RCC_CFGR_SWS_Pos        2U
#define RCC_CFGR_SWS_Msk        (0x3U << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_HSI        (0U << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_HSE        (1U << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_PLL        (2U << RCC_CFGR_SWS_Pos)

#define RCC_CFGR_HPRE_Pos       4U
#define RCC_CFGR_HPRE_Msk       (0xFU << RCC_CFGR_HPRE_Pos)

#define RCC_CFGR_PPRE1_Pos      10U
#define RCC_CFGR_PPRE1_Msk      (0x7U << RCC_CFGR_PPRE1_Pos)

#define RCC_CFGR_PPRE2_Pos      13U
#define RCC_CFGR_PPRE2_Msk      (0x7U << RCC_CFGR_PPRE2_Pos)

/* Flash interface register offsets */
#define FLASH_ACR_OFFSET        0x00U   /* Flash access control register */

/* FLASH_ACR register bits */
#define FLASH_ACR_LATENCY_Pos   0U
#define FLASH_ACR_LATENCY_Msk   (0xFU << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_PRFTEN        (1U << 8)   /* Prefetch enable */
#define FLASH_ACR_ICEN          (1U << 9)   /* Instruction cache enable */
#define FLASH_ACR_DCEN          (1U << 10)  /* Data cache enable */

/* Clock source definitions */
#define HSI_VALUE               16000000U   /* HSI oscillator frequency in Hz */
#define LSI_VALUE               32000U      /* LSI oscillator frequency in Hz */

/* Timeout values for clock operations */
#define HSI_STARTUP_TIMEOUT     5000U
#define HSE_STARTUP_TIMEOUT     5000U
#define PLL_STARTUP_TIMEOUT     5000U
#define CLOCKSWITCH_TIMEOUT     5000U
#define OVERDRIVE_STARTUP_TIMEOUT 5000U

/**
 * @brief RCC register structure (common layout)
 */
typedef struct {
    volatile uint32_t CR;           /* 0x00 - Clock control register */
    volatile uint32_t PLLCFGR;      /* 0x04 - PLL configuration register */
    volatile uint32_t CFGR;         /* 0x08 - Clock configuration register */
    volatile uint32_t CIR;          /* 0x0C - Clock interrupt register */
    volatile uint32_t RESERVED0[12]; /* 0x10..0x3C - resets/enables not needed here */
    volatile uint32_t APB1ENR;      /* 0x40 - APB1 peripheral clock enable register */
    /* Additional registers would follow but are not needed for basic clock config */
} RCC_TypeDef;

/**
 * @brief Flash register structure
 */
typedef struct {
    volatile uint32_t ACR;          /* Flash access control register */
    /* Additional registers would follow but ACR is main one needed */
} FLASH_TypeDef;

/**
 * @brief PWR (Power control) register structure, needed for Over-Drive mode
 * on STM32F7 parts running the core above their non-Over-Drive HCLK limit
 * (see RM0385: Over-Drive must be enabled above 180MHz).
 */
typedef struct {
    volatile uint32_t CR1;          /* 0x00 - Power control register 1 */
    volatile uint32_t CSR1;         /* 0x04 - Power control/status register 1 */
} PWR_TypeDef;

/* PWR_CR1 register bits */
#define PWR_CR1_ODEN            (1U << 16)  /* Over-Drive enable */
#define PWR_CR1_ODSWEN          (1U << 17)  /* Over-Drive switching enable */

/* PWR_CSR1 register bits */
#define PWR_CSR1_ODRDY          (1U << 16)  /* Over-Drive ready */
#define PWR_CSR1_ODSWRDY        (1U << 17)  /* Over-Drive switching ready */

#endif // STM32_COMMON_REGS_H
