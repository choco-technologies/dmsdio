#ifndef DMSDIO_TYPES_H
#define DMSDIO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @file dmsdio_types.h
 * @brief Types shared by dmsdio, dmsdio_port and dmsdio consumers.
 *
 * Everything that describes SD protocol behavior (command sequencing,
 * register layouts, addressing) is interpreted exclusively by the core
 * dmsdio module. The port-facing types below only describe *how* to clock a
 * command and its data phase, never *which* command to send.
 */

/** Logical block size used by every SD memory card supported by dmsdio. */
#define DMSDIO_BLOCK_SIZE           512u

/**
 * Buffer alignment for direct (zero-copy) port data transfers. 16 bytes lets
 * STM32 ports move data with 4-word DMA bursts that never cross a 1 KB
 * boundary; misaligned caller buffers are bounced through an aligned block.
 */
#define DMSDIO_TRANSFER_ALIGNMENT   16u

/** SD host controller instance (1-based, e.g. 1 = SDMMC1/SDIO). */
typedef uint8_t dmsdio_instance_t;

/* ======================================================================
 *  Port-facing types
 * ====================================================================== */

/** Data bus width. */
typedef enum
{
    dmsdio_bus_width_1bit = 1,  /**< DAT0 only */
    dmsdio_bus_width_4bit = 4,  /**< DAT0-DAT3 */
} dmsdio_bus_width_t;

/**
 * @brief Shape of the response a port must capture for a command.
 *
 * The port never interprets response content. It only has to know how many
 * bits to capture and whether the controller's CRC check applies.
 */
typedef enum
{
    dmsdio_response_none = 0,       /**< No response expected */
    dmsdio_response_short,          /**< 48-bit response, CRC checked */
    dmsdio_response_short_busy,     /**< 48-bit response followed by busy on DAT0, CRC checked */
    dmsdio_response_short_no_crc,   /**< 48-bit response whose CRC field is not valid */
    dmsdio_response_long,           /**< 136-bit response, CRC checked */
} dmsdio_response_type_t;

/** Direction of a data phase. */
typedef enum
{
    dmsdio_direction_read = 0,      /**< Card to host */
    dmsdio_direction_write,         /**< Host to card */
} dmsdio_direction_t;

/**
 * @brief Result of a port-level operation.
 *
 * These are raw transport outcomes. The core module maps them to errno
 * values and decides whether a retry makes sense.
 */
typedef enum
{
    dmsdio_status_ok = 0,           /**< Command (and data phase) completed */
    dmsdio_status_cmd_timeout,      /**< No response within the command timeout */
    dmsdio_status_cmd_crc,          /**< Response CRC mismatch */
    dmsdio_status_data_timeout,     /**< Data phase did not complete in time */
    dmsdio_status_data_crc,         /**< Data block CRC mismatch */
    dmsdio_status_overrun,          /**< FIFO overrun/underrun or DMA fault */
    dmsdio_status_invalid,          /**< Invalid parameters passed to the port */
    dmsdio_status_not_supported,    /**< Operation not supported by the controller */
} dmsdio_status_t;

/** A single command to clock out on the CMD line. */
typedef struct
{
    uint8_t                 index;      /**< Command index (0-63) */
    uint32_t                argument;   /**< 32-bit command argument */
    dmsdio_response_type_t  response;   /**< Expected response shape */
} dmsdio_command_t;

/**
 * @brief Optional data phase attached to a command.
 *
 * The port arms the data path before issuing the command so that a read
 * data block following the response is never missed.
 */
typedef struct
{
    void*               buffer;         /**< Source/destination, DMSDIO_TRANSFER_ALIGNMENT aligned */
    uint32_t            block_size;     /**< Bytes per block (1-2048) */
    uint32_t            block_count;    /**< Number of blocks (>= 1) */
    dmsdio_direction_t  direction;      /**< Transfer direction */
    uint32_t            timeout_ms;     /**< Upper bound for each block (read access or write busy) */
} dmsdio_data_t;

/**
 * @brief Captured command response.
 *
 * Short responses store the 32-bit payload (bits 39:8) in words[0].
 * Long responses store bits 127:0 of the 128-bit register, most significant
 * word first (words[0] = bits 127:96), with bit 0 undefined.
 */
typedef struct
{
    uint32_t    words[4];   /**< Response payload */
    uint8_t     index;      /**< Command index field of a short response */
} dmsdio_response_t;

/* ======================================================================
 *  Card description
 * ====================================================================== */

/** Detected card family. */
typedef enum
{
    dmsdio_card_type_none = 0,      /**< No card attached */
    dmsdio_card_type_sdsc_v1,       /**< SD 1.x standard capacity, byte addressed */
    dmsdio_card_type_sdsc_v2,       /**< SD 2.0+ standard capacity, byte addressed */
    dmsdio_card_type_sdhc,          /**< High capacity (<= 32 GiB), block addressed */
    dmsdio_card_type_sdxc,          /**< Extended capacity (> 32 GiB), block addressed */
} dmsdio_card_type_t;

/** Decoded Card Identification register (CID). */
typedef struct
{
    uint8_t     manufacturer_id;    /**< MID */
    char        oem_id[3];          /**< OID, null terminated */
    char        product_name[6];    /**< PNM, null terminated */
    uint8_t     product_revision;   /**< PRV (BCD n.m) */
    uint32_t    serial_number;      /**< PSN */
    uint16_t    manufacture_year;   /**< Full year (2000-2255) */
    uint8_t     manufacture_month;  /**< 1-12 */
} dmsdio_cid_t;

/** Decoded Card Specific Data register (CSD). */
typedef struct
{
    uint8_t     structure;          /**< CSD_STRUCTURE (0 = v1, 1 = v2) */
    uint8_t     tran_speed;         /**< TRAN_SPEED raw code */
    uint16_t    ccc;                /**< Card command classes bitmap */
    uint8_t     read_bl_len;        /**< READ_BL_LEN (log2 of bytes) */
    uint64_t    capacity_bytes;     /**< User area capacity */
    bool        erase_blk_en;       /**< ERASE_BLK_EN */
    uint8_t     sector_size;        /**< SECTOR_SIZE + 1 (in write blocks) */
    bool        perm_write_protect; /**< PERM_WRITE_PROTECT */
    bool        tmp_write_protect;  /**< TMP_WRITE_PROTECT */
} dmsdio_csd_t;

/** Decoded SD Configuration Register (SCR). */
typedef struct
{
    uint8_t     sd_spec;                /**< SD_SPEC */
    bool        sd_spec3;               /**< SD_SPEC3 */
    bool        data_stat_after_erase;  /**< Erased data reads as 1s when true */
    bool        bus_width_1bit;         /**< 1-bit bus supported */
    bool        bus_width_4bit;         /**< 4-bit bus supported */
    bool        cmd23_supported;        /**< SET_BLOCK_COUNT supported */
} dmsdio_scr_t;

/** Decoded SD Status register (SSR) fields used by the driver. */
typedef struct
{
    uint8_t     bus_width_code;     /**< DAT_BUS_WIDTH (0 = 1-bit, 2 = 4-bit) */
    uint8_t     speed_class;        /**< SPEED_CLASS */
    uint32_t    au_size_bytes;      /**< Allocation unit size, 0 = undefined */
    uint16_t    erase_size;         /**< ERASE_SIZE (AUs per erase timeout), 0 = unsupported */
    uint8_t     erase_timeout_s;    /**< ERASE_TIMEOUT in seconds */
    uint8_t     erase_offset_s;     /**< ERASE_OFFSET in seconds */
    bool        discard_supported;  /**< DISCARD_SUPPORT */
} dmsdio_ssr_t;

/** Snapshot of the attached card, see dmsdio_ioctl_cmd_get_card_info. */
typedef struct
{
    dmsdio_card_type_t  type;               /**< Card family */
    uint32_t            generation;         /**< Card generation identifier */
    uint16_t            rca;                /**< Relative card address */
    uint32_t            ocr;                /**< Operating conditions register */
    uint32_t            cid_raw[4];         /**< Raw CID, MSW first */
    uint32_t            csd_raw[4];         /**< Raw CSD, MSW first */
    dmsdio_cid_t        cid;                /**< Decoded CID */
    dmsdio_csd_t        csd;                /**< Decoded CSD */
    dmsdio_scr_t        scr;                /**< Decoded SCR */
    dmsdio_ssr_t        ssr;                /**< Decoded SD Status */
    uint64_t            capacity_bytes;     /**< Addressable capacity */
    uint64_t            block_count;        /**< Capacity in DMSDIO_BLOCK_SIZE blocks */
    bool                block_addressing;   /**< SDHC/SDXC block addressing */
    bool                high_speed;         /**< High Speed mode active */
    bool                write_protected;    /**< CSD write protection active */
    dmsdio_bus_width_t  bus_width;          /**< Negotiated data bus width */
    uint32_t            clock_hz;           /**< Actual bus clock */
} dmsdio_card_info_t;

/** Host controller state, see dmsdio_ioctl_cmd_get_host_info. */
typedef struct
{
    dmsdio_instance_t   instance;       /**< Controller instance */
    bool                card_attached;  /**< A card is identified and exposed */
    uint32_t            generation;     /**< Changes on every attach and detach */
    uint32_t            scan_count;     /**< Completed presence scans */
    int                 last_error;     /**< Last transport error (negative errno) or 0 */
    uint32_t            retry_count;    /**< Transfers recovered by a retry */
} dmsdio_host_info_t;

/* ======================================================================
 *  Driver-specific ioctl commands (start at DMDRVI_IOCTL_CUSTOM_BASE)
 * ====================================================================== */

/** Base of the dmsdio private ioctl range (== DMDRVI_IOCTL_CUSTOM_BASE). */
#define DMSDIO_IOCTL_BASE   0x1000

typedef enum
{
    /** arg: dmsdio_host_info_t* (host and card nodes) */
    dmsdio_ioctl_cmd_get_host_info = DMSDIO_IOCTL_BASE,
    /** arg: dmsdio_card_info_t* (host and card nodes, -ENODEV without a card) */
    dmsdio_ioctl_cmd_get_card_info,
    /** arg: NULL. Synchronously re-check presence (host node only). */
    dmsdio_ioctl_cmd_rescan,
} dmsdio_ioctl_cmd_t;

#endif /* DMSDIO_TYPES_H */
