#ifndef DMSDIO_SD_H
#define DMSDIO_SD_H

/*
 * SD memory card protocol constants (SD Physical Layer Simplified
 * Specification, sections 4.7 "Commands" and 4.10 "Card Status").
 * Private to the core module - ports never see these.
 */

/* --- Commands --- */
#define SD_CMD_GO_IDLE_STATE        0
#define SD_CMD_ALL_SEND_CID         2
#define SD_CMD_SEND_RELATIVE_ADDR   3
#define SD_CMD_SWITCH_FUNC          6
#define SD_CMD_SELECT_CARD          7
#define SD_CMD_SEND_IF_COND         8
#define SD_CMD_SEND_CSD             9
#define SD_CMD_STOP_TRANSMISSION    12
#define SD_CMD_SEND_STATUS          13
#define SD_CMD_SET_BLOCKLEN         16
#define SD_CMD_READ_SINGLE_BLOCK    17
#define SD_CMD_READ_MULTIPLE_BLOCK  18
#define SD_CMD_WRITE_BLOCK          24
#define SD_CMD_WRITE_MULTIPLE_BLOCK 25
#define SD_CMD_ERASE_WR_BLK_START   32
#define SD_CMD_ERASE_WR_BLK_END     33
#define SD_CMD_ERASE                38
#define SD_CMD_APP_CMD              55

/* --- Application commands (preceded by CMD55) --- */
#define SD_ACMD_SET_BUS_WIDTH       6
#define SD_ACMD_SD_STATUS           13
#define SD_ACMD_SD_SEND_OP_COND     41
#define SD_ACMD_SET_CLR_CARD_DETECT 42
#define SD_ACMD_SEND_SCR            51

/* --- CMD8 (SEND_IF_COND) --- */
#define SD_IF_COND_VHS_27_36        0x100u
#define SD_IF_COND_CHECK_PATTERN    0xAAu

/* --- OCR / ACMD41 --- */
#define SD_OCR_VOLTAGE_WINDOW       0x00FF8000u     /* 2.7 - 3.6 V */
#define SD_OCR_HCS                  0x40000000u     /* Host capacity support / CCS */
#define SD_OCR_BUSY                 0x80000000u     /* Power-up complete */

/* --- R1 card status bits --- */
#define SD_R1_OUT_OF_RANGE          (1u << 31)
#define SD_R1_ADDRESS_ERROR         (1u << 30)
#define SD_R1_BLOCK_LEN_ERROR       (1u << 29)
#define SD_R1_ERASE_SEQ_ERROR       (1u << 28)
#define SD_R1_ERASE_PARAM           (1u << 27)
#define SD_R1_WP_VIOLATION          (1u << 26)
#define SD_R1_LOCK_UNLOCK_FAILED    (1u << 24)
#define SD_R1_COM_CRC_ERROR         (1u << 23)
#define SD_R1_ILLEGAL_COMMAND       (1u << 22)
#define SD_R1_CARD_ECC_FAILED       (1u << 21)
#define SD_R1_CC_ERROR              (1u << 20)
#define SD_R1_ERROR                 (1u << 19)
#define SD_R1_CSD_OVERWRITE         (1u << 16)
#define SD_R1_WP_ERASE_SKIP         (1u << 15)
#define SD_R1_READY_FOR_DATA        (1u << 8)
#define SD_R1_APP_CMD               (1u << 5)
#define SD_R1_AKE_SEQ_ERROR         (1u << 3)

#define SD_R1_ERRORS                0xFDF98008u
#define SD_R1_STATE(status)         (((status) >> 9) & 0xFu)

/* --- R6 (SEND_RELATIVE_ADDR) status bits 23, 22, 19 --- */
#define SD_R6_ERRORS                0xE000u

/* --- Card states --- */
#define SD_STATE_IDLE               0u
#define SD_STATE_READY              1u
#define SD_STATE_IDENT              2u
#define SD_STATE_STBY               3u
#define SD_STATE_TRAN               4u
#define SD_STATE_DATA               5u
#define SD_STATE_RCV                6u
#define SD_STATE_PRG                7u

/* --- CMD6 (SWITCH_FUNC) --- */
#define SD_SWITCH_CHECK             0x00FFFFF0u
#define SD_SWITCH_SET               0x80FFFFF0u
#define SD_SWITCH_HIGH_SPEED        0x1u
#define SD_SWITCH_STATUS_SIZE       64u

/* --- Card command classes (CSD CCC) --- */
#define SD_CCC_ERASE                (1u << 5)
#define SD_CCC_SWITCH               (1u << 10)

/* --- CMD38 arguments --- */
#define SD_ERASE_ARG                0x00000000u
#define SD_DISCARD_ARG              0x00000001u

/* --- Registers transferred on the data lines --- */
#define SD_SCR_SIZE                 8u
#define SD_SSR_SIZE                 64u

/* --- Clocks --- */
#define SD_CLOCK_IDENTIFICATION_HZ  400000u
#define SD_CLOCK_DEFAULT_SPEED_HZ   25000000u
#define SD_CLOCK_HIGH_SPEED_HZ      50000000u

/* --- Timing --- */
#define SD_POWER_UP_DELAY_MS        2u      /* >= 1 ms + 74 clocks at 400 kHz */
#define SD_ACMD41_POLL_MS           10u

#endif /* DMSDIO_SD_H */
