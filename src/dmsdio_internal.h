#ifndef DMSDIO_INTERNAL_H
#define DMSDIO_INTERNAL_H

#include "dmod.h"
#include "dmsdio.h"
#include "dmsdio_port.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmosi.h"
#include "dmgpio_types.h"
#include <errno.h>

/*
 * Only dmsdio.c defines DMOD_ENABLE_REGISTRATION before including this
 * header: it owns the API registration records for every header listed
 * above, and therefore also every dmdrvi DIF implementation.
 */

/* Magic numbers - 'SDIO' for driver contexts, 'SDFH' for open handles. */
#define DMSDIO_CONTEXT_MAGIC        0x5344494Fu
#define DMSDIO_HANDLE_MAGIC         0x53444648u

/* Card node minor number below the host node (/dev/dmsdioN/0). */
#define DMSDIO_CARD_MINOR           0u

/**
 * @brief Configuration read from the dmdrvi ini section.
 */
typedef struct
{
    dmsdio_instance_t   instance;               /**< Controller instance (1-based) */
    uint8_t             major;                  /**< Device major number */
    dmsdio_bus_width_t  max_bus_width;          /**< Widest bus the board wires up */
    uint32_t            max_clock_hz;           /**< Upper clock limit */
    bool                high_speed;             /**< Allow CMD6 High Speed switch */
    uint32_t            retries;                /**< Extra attempts for transient errors */
    uint32_t            init_timeout_ms;        /**< ACMD41 power-up limit */
    uint32_t            read_timeout_ms;        /**< Read data phase limit */
    uint32_t            write_timeout_ms;       /**< Write data phase / busy limit */
    uint32_t            erase_timeout_ms;       /**< Minimum erase busy limit */
    uint32_t            max_blocks_per_transfer;/**< Split larger requests */
    bool                cd_active_high;         /**< Card detect polarity */
    /* Presence monitoring policy - not used by the driver itself, handed to
     * the monitor service through DMDRVI_IOCTL_MONITOR_GET_POLICY. */
    dmdrvi_monitor_policy_t monitor;
} dmsdio_config_t;

/**
 * @brief Driver context, one per configured host controller.
 */
struct dmdrvi_context
{
    uint32_t            magic;          /**< DMSDIO_CONTEXT_MAGIC */
    dmsdio_config_t     config;         /**< Parsed configuration */
    dmosi_mutex_t       lock;           /**< Serializes all bus operations */
    uint8_t*            scratch;        /**< One aligned block for RMW/bounce */
    dmsdio_card_info_t  card;           /**< Attached card, type none if absent */
    uint32_t            generation;     /**< Bumped on every attach/detach */
    uint32_t            scan_count;     /**< Completed presence scans */
    int                 last_error;     /**< Last transport error */
    uint32_t            retry_count;    /**< Operations recovered by retry */
    volatile bool       removal_pending;/**< Card detect says removed; set without the lock */
    bool                host_ready;     /**< dmdevfs knows the host node (path_ready) */
    bool                card_announced; /**< Card node announced to dmdevfs */
    uint32_t            announced_generation; /**< Card generation the announcement is for */
    dmosi_mutex_t       cd_lock;        /**< Guards cd_path (never held across bus work) */
    char*               cd_path;        /**< Card detect GPIO node, NULL = none */
};

/** Open file handle - pins the card generation it was opened for. */
typedef struct
{
    uint32_t    magic;          /**< DMSDIO_HANDLE_MAGIC */
    bool        is_card;        /**< Card node (true) or host node */
    uint32_t    generation;     /**< Card generation at open time */
    int         flags;          /**< DMDRVI_O_* */
} dmsdio_handle_t;

/* --- dmsdio_config.c --- */
int  dmsdio_config_read(dmini_context_t ini, dmsdio_config_t* config);

/* --- dmsdio_cmd.c: single commands and card status --- */
int  dmsdio_cmd_errno(dmsdio_status_t status);
int  dmsdio_cmd_send(struct dmdrvi_context* ctx, uint8_t index, uint32_t arg,
                     dmsdio_response_type_t type, dmsdio_response_t* resp);
int  dmsdio_cmd_app(struct dmdrvi_context* ctx, uint8_t index, uint32_t arg,
                    dmsdio_response_type_t type, dmsdio_response_t* resp);
int  dmsdio_cmd_data(struct dmdrvi_context* ctx, bool app, uint8_t index, uint32_t arg,
                     const dmsdio_data_t* data, dmsdio_response_t* resp);
int  dmsdio_cmd_status(struct dmdrvi_context* ctx, uint32_t* status);
int  dmsdio_cmd_wait_ready(struct dmdrvi_context* ctx, uint32_t timeout_ms);
int  dmsdio_cmd_stop(struct dmdrvi_context* ctx);

/* --- dmsdio_ident.c: identification and bus negotiation --- */
int  dmsdio_ident_run(struct dmdrvi_context* ctx, dmsdio_card_info_t* card);

/* --- dmsdio_xfer.c: block transfers, erase and recovery --- */
int  dmsdio_xfer_read(struct dmdrvi_context* ctx, uint64_t lba, void* buf, uint32_t count);
int  dmsdio_xfer_write(struct dmdrvi_context* ctx, uint64_t lba, const void* buf, uint32_t count);
int  dmsdio_xfer_erase(struct dmdrvi_context* ctx, uint64_t lba, uint64_t count, bool discard);

/* --- dmsdio_io.c: byte-offset I/O --- */
dmdrvi_ssize_t dmsdio_io_read(struct dmdrvi_context* ctx, uint8_t* buf, size_t size, uint64_t offset);
dmdrvi_ssize_t dmsdio_io_write(struct dmdrvi_context* ctx, const uint8_t* buf, size_t size, uint64_t offset);
int  dmsdio_io_erase_range(struct dmdrvi_context* ctx, const dmdrvi_block_range_t* range, bool discard);

/* --- dmsdio_card.c: attach/detach bookkeeping --- */
int  dmsdio_card_scan(struct dmdrvi_context* ctx);
void dmsdio_card_detach(struct dmdrvi_context* ctx);
int  dmsdio_card_lost(struct dmdrvi_context* ctx, int error);
bool dmsdio_card_attached(const struct dmdrvi_context* ctx);

/* --- dmsdio_detect.c: card detect pin --- */
void dmsdio_detect_set_cd_path(struct dmdrvi_context* ctx, const char* path);
int  dmsdio_detect_read_cd(struct dmdrvi_context* ctx, bool* present);
int  dmsdio_detect_event(struct dmdrvi_context* ctx);
void dmsdio_detect_release(struct dmdrvi_context* ctx);

/* --- helpers --- */
static inline void dmsdio_lock(struct dmdrvi_context* ctx)   { dmosi_mutex_lock(ctx->lock); }
static inline void dmsdio_unlock(struct dmdrvi_context* ctx) { dmosi_mutex_unlock(ctx->lock); }

static inline uint32_t dmsdio_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

#endif /* DMSDIO_INTERNAL_H */
