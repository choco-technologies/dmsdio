#ifndef DMSDIO_TYPES_H
#define DMSDIO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief SD host controller instance (0-based, e.g. 0 = SDMMC1/SDIO1)
 */
typedef uint8_t dmsdio_instance_t;

/**
 * @brief Card generation counter
 *
 * Bumped every time a card is (re)identified after insertion. A handle or
 * cached value tagged with a generation older than the context's current
 * one refers to a card that is no longer there (removed, or replaced with a
 * different card while nobody was watching) - see "card-generation
 * identifier" in dmsdio.c.
 */
typedef uint32_t dmsdio_generation_t;

/**
 * @brief Card capacity/addressing family
 *
 * Determines whether CMD17/18/24/25's argument is a byte offset (SDSC) or a
 * 512-byte block index (SDHC/SDXC) - see CMD_ARGUMENT in the Physical Layer
 * spec. Never set by a port - only dmsdio.c's own CMD8/ACMD41/CMD58(OCR)
 * sequencing determines it.
 */
typedef enum
{
    dmsdio_card_type_none = 0,  /**< No card identified (socket empty, or identification failed) */
    dmsdio_card_type_sdsc_v1,   /**< SDSC, pre-2.0 card (no CMD8 response) - byte addressed */
    dmsdio_card_type_sdsc_v2,   /**< SDSC, 2.0+ card, OCR CCS=0 - byte addressed */
    dmsdio_card_type_sdhc,      /**< SDHC, OCR CCS=1, capacity <= 32 GiB - block addressed */
    dmsdio_card_type_sdxc,      /**< SDXC, OCR CCS=1, capacity > 32 GiB - block addressed */
} dmsdio_card_type_t;

/**
 * @brief Data bus width
 */
typedef enum
{
    dmsdio_bus_width_1bit = 0,
    dmsdio_bus_width_4bit,
} dmsdio_bus_width_t;

/**
 * @brief Bus speed mode
 *
 * dmsdio_speed_mode_identification is only ever used internally during
 * dmsdio.c's own card-identification sequence (CMD0..ACMD41) - it is not a
 * valid argument to dmsdio_port_set_speed_mode() from anywhere else.
 */
typedef enum
{
    dmsdio_speed_mode_identification = 0,  /**< <= 400 kHz, open-drain-safe - CMD0..ACMD41 only */
    dmsdio_speed_mode_default_speed,       /**< <= 25 MHz */
    dmsdio_speed_mode_high_speed,          /**< <= 50 MHz - only after a successful CMD6 switch */
} dmsdio_speed_mode_t;

/**
 * @brief Expected response shape for a command sent through dmsdio_port_send_command()
 *
 * Matches the SD Physical Layer spec's response types. The port only needs
 * to know how many bits to clock in and where the CRC/index-check bits are -
 * it does not interpret payload bits (OCR fields, RCA, card status, etc.);
 * that is dmsdio.c's job.
 */
typedef enum
{
    dmsdio_response_none = 0,  /**< No response expected (e.g. CMD0) */
    dmsdio_response_r1,        /**< 48-bit, card status - normal command response */
    dmsdio_response_r1b,       /**< R1, plus the card may hold DAT0 low afterward (busy) */
    dmsdio_response_r2,        /**< 136-bit - CID or CSD register contents */
    dmsdio_response_r3,        /**< 48-bit, OCR register, no CRC (CRC field is all 1s) */
    dmsdio_response_r6,        /**< 48-bit, published RCA + a 16-bit status subset */
    dmsdio_response_r7,        /**< 48-bit, card interface condition (echoed check pattern + voltage) */
} dmsdio_response_type_t;

/**
 * @brief Raw response payload from dmsdio_port_send_command()
 *
 * A big-endian 128-bit container, right-aligned: words[3] is always the
 * least-significant 32 bits, words[0] the most-significant. The port
 * strips all framing (start bit, transmission bit, command index/reserved
 * bits, CRC7, end bit) before filling this in - only content bits remain:
 *
 *   - dmsdio_response_none: unused (all callers pass response == NULL)
 *   - dmsdio_response_r1/_r1b/_r3/_r6/_r7 (48-bit response, 32 content
 *     bits): words[3] holds the 32-bit content, words[0..2] are 0
 *   - dmsdio_response_r2 (136-bit response, the full 128-bit CID or CSD
 *     register as its content): words[0] holds bits [127:96], ...,
 *     words[3] holds bits [31:0] - exactly how the Physical Layer spec
 *     itself tables CID/CSD field bit ranges
 *
 * dmsdio.c's response parsers (parse_r1_card_status()/parse_r3_ocr()/
 * parse_cid()/parse_csd()/...) know the exact field layout within that for
 * each type.
 */
typedef struct
{
    uint32_t words[4]; /**< words[0] = most significant, words[3] = least significant */
} dmsdio_response_t;

/**
 * @brief Error a port primitive can report
 *
 * A superset of what any single primitive can actually return - e.g.
 * dmsdio_port_send_command() never returns dmsdio_error_data_crc. Kept in
 * one enum (rather than per-primitive enums) so dmsdio.c's retry/recovery
 * logic in dmsdio.c can treat every port call's result uniformly.
 */
typedef enum
{
    dmsdio_error_none = 0,
    dmsdio_error_no_response,      /**< Command timed out waiting for a response */
    dmsdio_error_command_crc,      /**< Response CRC (or index) check failed */
    dmsdio_error_data_timeout,     /**< Data phase timed out (no card activity within the deadline) */
    dmsdio_error_data_crc,         /**< Data block CRC check failed */
    dmsdio_error_removed,          /**< Card was physically removed mid-operation */
    dmsdio_error_invalid_argument, /**< Bad argument to a primitive (e.g. block_count == 0) */
    dmsdio_error_not_supported,    /**< This port/hardware cannot do what was asked */
} dmsdio_error_t;

/**
 * @brief SD sector size in bytes
 *
 * Fixed at 512 for every card family this driver supports - SDHC/SDXC are
 * defined to always use 512-byte blocks, and every SDSC card in practice
 * accepts CMD16 SET_BLOCKLEN(512) even though its CSD's READ_BL_LEN may
 * advertise a larger native block size.
 */
#define DMSDIO_BLOCK_SIZE 512U

/**
 * @brief Parsed CID (Card Identification) register - R2 response to CMD2
 */
typedef struct
{
    uint8_t  manufacturer_id;      /**< MID */
    char     oem_id[3];            /**< OID, NUL-terminated */
    char     product_name[6];      /**< PNM, NUL-terminated */
    uint8_t  product_revision;     /**< PRV, packed BCD (major<<4 | minor) */
    uint32_t serial_number;        /**< PSN */
    uint16_t manufacturing_year;   /**< Decoded from MDT (base year 2000) */
    uint8_t  manufacturing_month;  /**< Decoded from MDT, 1-12 */
} dmsdio_cid_t;

/**
 * @brief Parsed fields of the CSD (Card-Specific Data) register actually
 *        used by this driver - R2 response to CMD9
 *
 * capacity_blocks and max_transfer_speed_hz are derived from different CSD
 * fields depending on csd_structure_version (1.0 for SDSC, 2.0 for
 * SDHC/SDXC) - dmsdio.c's csd parser hides that difference here.
 */
typedef struct
{
    uint8_t  csd_structure_version; /**< 0 = CSD 1.0 (SDSC), 1 = CSD 2.0 (SDHC/SDXC) */
    uint64_t capacity_blocks;       /**< Total addressable 512-byte blocks */
    uint32_t max_transfer_speed_hz; /**< Decoded from TAAC/TRAN_SPEED */
    bool     read_only;             /**< Permanent write protection (CSD PERM_WRITE_PROTECT) */
} dmsdio_csd_t;

/**
 * @brief Parsed fields of the SCR (SD Configuration Register) actually used
 *        by this driver - read via ACMD51 (a 512-bit-wide single-block data
 *        transfer, not a command response)
 */
typedef struct
{
    uint8_t sd_spec_version;    /**< SD_SPEC/SD_SPEC3/SD_SPEC4/SD_SPECX, encoded as the physical spec's minor version */
    bool    supports_4bit_bus;  /**< SD_BUS_WIDTHS bit 2 */
} dmsdio_scr_t;

/**
 * @brief Snapshot of everything dmsdio.c knows about the currently inserted
 *        card - returned by dmsdio_ioctl_cmd_get_card_info
 */
typedef struct
{
    dmsdio_card_type_t   card_type;
    dmsdio_generation_t  generation;
    dmsdio_cid_t         cid;
    dmsdio_csd_t         csd;
    dmsdio_scr_t         scr;
    dmsdio_bus_width_t   bus_width;
    dmsdio_speed_mode_t  speed_mode;
} dmsdio_card_info_t;

/**
 * @brief IOCTL commands for the DMSDIO device
 *
 * Sent to the card sub-node (/dev/dmsdioN/0), not the host context itself.
 */
typedef enum
{
    dmsdio_ioctl_cmd_get_card_info = 1, /**< arg = dmsdio_card_info_t* - snapshot of the current card */
    dmsdio_ioctl_cmd_get_generation,    /**< arg = dmsdio_generation_t* - current card generation */
    dmsdio_ioctl_cmd_erase,             /**< arg = dmsdio_erase_range_t* - erase a byte range (CMD32/33/38) */

    dmsdio_ioctl_cmd_max
} dmsdio_ioctl_cmd_t;

/**
 * @brief Byte range to erase, used with dmsdio_ioctl_cmd_erase
 */
typedef struct
{
    uint64_t offset; /**< Must be block-aligned (a multiple of DMSDIO_BLOCK_SIZE) */
    uint64_t size;   /**< Must be a multiple of DMSDIO_BLOCK_SIZE */
} dmsdio_erase_range_t;

/**
 * @brief Opaque driver context type (forward declaration)
 */
struct dmdrvi_context;
typedef struct dmdrvi_context *dmdrvi_context_t;

#endif /* DMSDIO_TYPES_H */
