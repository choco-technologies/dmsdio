#define DMOD_ENABLE_REGISTRATION ON
#include "dmsdio_internal.h"
#include "dmsdio_sd.h"
#include <stdint.h>
#include <string.h>

/*
 * dmdrvi 2.0 device model.
 *
 *   /dev/dmsdioN     persistent host node (major only). Status and monitor ioctls.
 *   /dev/dmsdioN/0   card node (major+minor 0), announced through
 *                    dmdrvi_device_available() once a card is identified and
 *                    withdrawn through dmdrvi_device_unavailable() when it
 *                    goes away. Byte-addressed 64-bit block device.
 *
 * All bus operations of one host are serialized by ctx->lock. Handles to the
 * card node carry the card generation they were opened for.
 *
 * The driver starts no threads of its own and implements the dmdrvi 2.1
 * monitor contract on the host node instead: GET_POLICY hands out the
 * presence policy from its ini section, EVENT samples the card detect pin
 * without the lock, REFRESH identifies, verifies or detaches the card and is
 * the only place the card node is announced or withdrawn. dmdevfs reports
 * the host node as a "monitor" device, and the monitor service it starts
 * (dmdevmon) calls these - including one REFRESH right away, which picks up
 * a card present at boot.
 */

#define SCRATCH_ALIGNMENT   32u     /* cache line: safe for DMA cache maintenance */

static bool is_valid_context(struct dmdrvi_context* ctx)
{
    return ctx != NULL && ctx->magic == DMSDIO_CONTEXT_MAGIC;
}

static bool is_valid_handle(const dmsdio_handle_t* handle)
{
    return handle != NULL && handle->magic == DMSDIO_HANDLE_MAGIC;
}

/* Lock held: is the handle's card still the attached one? */
static int check_card_handle(struct dmdrvi_context* ctx, const dmsdio_handle_t* handle)
{
    if (!handle->is_card)
    {
        return -ENOTSUP;
    }
    if (!dmsdio_card_attached(ctx))
    {
        return -ENODEV;
    }
    return (handle->generation == ctx->card.generation) ? 0 : -ESTALE;
}

static void destroy_context(struct dmdrvi_context* ctx, bool port_ready)
{
    dmsdio_detect_release(ctx);
    if (port_ready)
    {
        dmsdio_port_set_power(ctx->config.instance, false);
        dmsdio_port_host_deinit(ctx->config.instance);
    }
    if (ctx->lock != NULL)
    {
        dmosi_mutex_destroy(ctx->lock);
    }
    if (ctx->cd_lock != NULL)
    {
        dmosi_mutex_destroy(ctx->cd_lock);
    }
    Dmod_Free(ctx->scratch);
    ctx->magic = 0;
    Dmod_Free(ctx);
}

static int allocate_resources(struct dmdrvi_context* ctx)
{
    ctx->scratch = Dmod_AlignedMalloc(DMSDIO_BLOCK_SIZE, SCRATCH_ALIGNMENT);
    ctx->lock    = dmosi_mutex_create(false);
    ctx->cd_lock = dmosi_mutex_create(false);
    if (ctx->scratch == NULL || ctx->lock == NULL || ctx->cd_lock == NULL)
    {
        DMOD_LOG_ERROR("dmsdio: out of memory\n");
        return -ENOMEM;
    }
    return 0;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}

/* ---- dmdrvi: context ---- */

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, dmdrvi_context_t, _create,
    ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (config == NULL || dev_num == NULL)
    {
        return NULL;
    }
    struct dmdrvi_context* ctx = Dmod_Malloc(sizeof(*ctx));
    if (ctx == NULL)
    {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->magic = DMSDIO_CONTEXT_MAGIC;

    if (dmsdio_config_read(config, &ctx->config) != 0 || allocate_resources(ctx) != 0)
    {
        destroy_context(ctx, false);
        return NULL;
    }
    int ret = dmsdio_port_host_init(ctx->config.instance);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmsdio: host %u initialization failed (%d)\n", (unsigned)ctx->config.instance, ret);
        destroy_context(ctx, false);
        return NULL;
    }

    memset(dev_num, 0, sizeof(*dev_num));
    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->major = ctx->config.major;
    return ctx;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void, _free, ( dmdrvi_context_t context ))
{
    if (!is_valid_context(context))
    {
        return;
    }
    /* dmdevfs is tearing this context down itself - no unavailable notice. */
    destroy_context(context, true);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void, _friend_changed,
    ( dmdrvi_context_t context, const dmdrvi_friend_info_t* info ))
{
    if (!is_valid_context(context) || info == NULL || info->friend_role == NULL ||
        strcmp(info->friend_role, "card_detect") != 0)
    {
        return;
    }
    bool ready = (info->state == dmdrvi_dev_state_ready && info->node_path != NULL);
    dmsdio_detect_set_cd_path(context, ready ? info->node_path : NULL);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void, _path_ready,
    ( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num, const char* path ))
{
    if (!is_valid_context(context) || dev_num == NULL || (dev_num->flags & DMDRVI_NUM_MINOR) != 0)
    {
        return;     /* only the host node's registration matters */
    }
    (void)path;
    /* From now on dmdevfs accepts the card node - the next REFRESH announces it. */
    dmsdio_lock(context);
    context->host_ready = true;
    dmsdio_unlock(context);
}

/* ---- dmdrvi: handles ---- */

static bool is_card_dev_num(const dmdrvi_dev_num_t* dev_num)
{
    return (dev_num->flags & DMDRVI_NUM_MINOR) != 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void*, _open,
    ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    if (!is_valid_context(context) || dev_num == NULL ||
        (is_card_dev_num(dev_num) && dev_num->minor != DMSDIO_CARD_MINOR))
    {
        return NULL;
    }
    dmsdio_handle_t* handle = Dmod_Malloc(sizeof(*handle));
    if (handle == NULL)
    {
        return NULL;
    }
    handle->magic   = DMSDIO_HANDLE_MAGIC;
    handle->is_card = is_card_dev_num(dev_num);
    handle->flags   = flags;

    dmsdio_lock(context);
    bool usable = !handle->is_card || dmsdio_card_attached(context);
    handle->generation = context->card.generation;
    dmsdio_unlock(context);

    if (!usable)
    {
        handle->magic = 0;
        Dmod_Free(handle);
        return NULL;
    }
    return handle;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void, _close, ( dmdrvi_context_t context, void* handle ))
{
    dmsdio_handle_t* h = (dmsdio_handle_t*)handle;
    if (!is_valid_context(context) || !is_valid_handle(h))
    {
        return;
    }
    h->magic = 0;
    Dmod_Free(h);
}

/* ---- dmdrvi: data path ---- */

static int check_io(struct dmdrvi_context* ctx, const dmsdio_handle_t* h, const void* buffer,
                    size_t size, dmdrvi_offset_t offset, int forbidden_flag)
{
    if (offset < 0)
    {
        return -EINVAL;
    }
#if SIZE_MAX > INT64_MAX
    if ((uint64_t)size > (uint64_t)INT64_MAX)
    {
        return -EOVERFLOW;
    }
#endif
    if (!is_valid_context(ctx) || !is_valid_handle(h) || (buffer == NULL && size != 0))
    {
        return -EINVAL;
    }
    if (!h->is_card)
    {
        return -ENOTSUP;
    }
    return (h->flags == forbidden_flag) ? -EBADF : 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, dmdrvi_ssize_t, _read,
    ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    dmsdio_handle_t* h = (dmsdio_handle_t*)handle;
    int ret = check_io(context, h, buffer, size, offset, DMDRVI_O_WRONLY);
    if (ret != 0 || size == 0)
    {
        return ret;
    }
    dmsdio_lock(context);
    dmdrvi_ssize_t result = check_card_handle(context, h);
    if (result == 0)
    {
        result = dmsdio_io_read(context, (uint8_t*)buffer, size, (uint64_t)offset);
    }
    dmsdio_unlock(context);
    return result;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, dmdrvi_ssize_t, _write,
    ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    dmsdio_handle_t* h = (dmsdio_handle_t*)handle;
    int ret = check_io(context, h, buffer, size, offset, DMDRVI_O_RDONLY);
    if (ret != 0 || size == 0)
    {
        return ret;
    }
    dmsdio_lock(context);
    dmdrvi_ssize_t result = check_card_handle(context, h);
    if (result == 0)
    {
        result = dmsdio_io_write(context, (const uint8_t*)buffer, size, (uint64_t)offset);
    }
    dmsdio_unlock(context);
    return result;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    dmsdio_handle_t* h = (dmsdio_handle_t*)handle;
    if (!is_valid_context(context) || !is_valid_handle(h))
    {
        return -EINVAL;
    }
    if (!h->is_card)
    {
        return 0;
    }
    dmsdio_lock(context);
    int ret = check_card_handle(context, h);
    if (ret == 0)
    {
        ret = dmsdio_cmd_wait_ready(context, context->config.write_timeout_ms);
    }
    dmsdio_unlock(context);
    return ret;
}

/* ---- dmdrvi: control ---- */

static void fill_host_info(struct dmdrvi_context* ctx, dmsdio_host_info_t* info)
{
    info->instance      = ctx->config.instance;
    info->card_attached = dmsdio_card_attached(ctx);
    info->generation    = ctx->generation;
    info->scan_count    = ctx->scan_count;
    info->last_error    = ctx->last_error;
    info->retry_count   = ctx->retry_count;
}

static void fill_block_info(struct dmdrvi_context* ctx, dmdrvi_block_info_t* info)
{
    const dmsdio_card_info_t* card = &ctx->card;
    bool erase = (card->csd.ccc & SD_CCC_ERASE) != 0;
    info->logical_block_size = DMSDIO_BLOCK_SIZE;
    info->erase_block_size   = erase ? DMSDIO_BLOCK_SIZE : 0;
    info->block_count        = card->block_count;
    info->flags              = DMDRVI_BLOCK_FLAG_REMOVABLE
                             | (erase ? DMDRVI_BLOCK_FLAG_ERASE_SUPPORTED : 0u)
                             | (card->ssr.discard_supported ? DMDRVI_BLOCK_FLAG_DISCARD_SUPPORTED : 0u)
                             | (card->write_protected ? DMDRVI_BLOCK_FLAG_READ_ONLY : 0u);
}

/* Lock held. Controls available on the host node (and the card node). */
static int host_ioctl(struct dmdrvi_context* ctx, const dmsdio_handle_t* h, int command, void* arg)
{
    switch (command)
    {
        case dmsdio_ioctl_cmd_get_host_info:
            fill_host_info(ctx, (dmsdio_host_info_t*)arg);
            return 0;
        case dmsdio_ioctl_cmd_get_card_info:
        {
            int ret = h->is_card ? check_card_handle(ctx, h)
                                 : (dmsdio_card_attached(ctx) ? 0 : -ENODEV);
            if (ret == 0)
            {
                *(dmsdio_card_info_t*)arg = ctx->card;
            }
            return ret;
        }
        default:
            return -ENOTTY;
    }
}

/* Lock held. Block device controls of the card node. */
static int card_ioctl(struct dmdrvi_context* ctx, const dmsdio_handle_t* h, int command, void* arg)
{
    int ret = check_card_handle(ctx, h);
    if (ret != 0)
    {
        return ret;
    }
    dmdrvi_block_info_t info;
    fill_block_info(ctx, &info);
    switch (command)
    {
        case DMDRVI_IOCTL_BLOCK_GET_INFO:
            *(dmdrvi_block_info_t*)arg = info;
            return 0;
        case DMDRVI_IOCTL_BLOCK_ERASE:
        case DMDRVI_IOCTL_BLOCK_DISCARD:
        {
            bool discard = (command == DMDRVI_IOCTL_BLOCK_DISCARD);
            uint32_t needed = discard ? DMDRVI_BLOCK_FLAG_DISCARD_SUPPORTED : DMDRVI_BLOCK_FLAG_ERASE_SUPPORTED;
            if ((info.flags & needed) == 0)
            {
                return -ENOTSUP;
            }
            return (h->flags == DMDRVI_O_RDONLY) ? -EBADF
                 : dmsdio_io_erase_range(ctx, (const dmdrvi_block_range_t*)arg, discard);
        }
        default:
            return -ENOTTY;
    }
}

static bool is_block_ioctl(int command)
{
    return command == DMDRVI_IOCTL_BLOCK_GET_INFO || command == DMDRVI_IOCTL_BLOCK_ERASE ||
           command == DMDRVI_IOCTL_BLOCK_DISCARD;
}

/*
 * dmdrvi monitor contract - host node only, so dmdevfs reports just the host
 * as a monitored node; on the card node these are unknown commands.
 */
static int monitor_ioctl(struct dmdrvi_context* ctx, int command, void* arg)
{
    switch (command)
    {
        case DMDRVI_IOCTL_MONITOR_GET_POLICY:
            if (arg == NULL)
            {
                return -EINVAL;
            }
            *(dmdrvi_monitor_policy_t*)arg = ctx->config.monitor;   /* constant after create */
            return 0;
        case DMDRVI_IOCTL_MONITOR_EVENT:
            /* Deliberately lock-free, see dmsdio_detect_event(). */
            return dmsdio_detect_event(ctx);
        case DMDRVI_IOCTL_MONITOR_REFRESH:
        {
            dmsdio_lock(ctx);
            int ret = dmsdio_card_scan(ctx);
            dmsdio_unlock(ctx);
            return ret;
        }
        default:
            return -ENOTTY;
    }
}

static bool is_monitor_ioctl(int command)
{
    return command == DMDRVI_IOCTL_MONITOR_GET_POLICY || command == DMDRVI_IOCTL_MONITOR_EVENT ||
           command == DMDRVI_IOCTL_MONITOR_REFRESH;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, int, _ioctl,
    ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    dmsdio_handle_t* h = (dmsdio_handle_t*)handle;
    if (!is_valid_context(context) || !is_valid_handle(h))
    {
        return -EINVAL;
    }
    if (is_monitor_ioctl(command))
    {
        return h->is_card ? -ENOTTY : monitor_ioctl(context, command, arg);
    }
    if (arg == NULL)
    {
        return -EINVAL;
    }
    dmsdio_lock(context);
    int ret = is_block_ioctl(command)
            ? (h->is_card ? card_ioctl(context, h, command, arg) : -ENOTTY)
            : host_ioctl(context, h, command, arg);
    dmsdio_unlock(context);
    return ret;
}

/* "/dev/dmsdio0/0" (or "/dmsdio0/0" relative to the mount) is the card node. */
static bool is_card_path(const char* path)
{
    const char* last = strrchr(path, '/');
    return last != NULL && last != path && strcmp(last + 1, "0") == 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, int, _stat,
    ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    if (!is_valid_context(context) || path == NULL || stat == NULL)
    {
        return -EINVAL;
    }
    stat->size = 0;
    stat->mode = 0666;
    if (!is_card_path(path))
    {
        return 0;
    }
    dmsdio_lock(context);
    int ret = dmsdio_card_attached(context) ? 0 : -ENODEV;
    if (ret == 0)
    {
        stat->size = context->card.capacity_bytes;
        stat->mode = context->card.write_protected ? 0444 : 0666;
    }
    dmsdio_unlock(context);
    return ret;
}
