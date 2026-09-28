#include "dmod.h"
#include "dmosi.h"
#include "dmhaman.h"
#include "dmsdio_types.h"
#include <errno.h>

/*
 * dmsdiod - SD card presence service, one instance per dmsdio host node.
 *
 * dmsdio reports its host node (/dev/dmsdioN) to libsystemd under the
 * "sdio" device class; the [class=sdio] rule in configs/dmsdiod.rules
 * starts dmsdiod@dmsdioN from configs/dmsdiod@.ini with the node's path as
 * the only argument. The driver itself never waits or polls - everything
 * over time happens here, through the host node's ioctls:
 *
 *   dmsdio_ioctl_cmd_get_detect_config  card_detect_handler, debounce, poll interval
 *   dmsdio_ioctl_cmd_check_removal      lock-free "slot empty?" - aborts a running transfer
 *   dmsdio_ioctl_cmd_rescan             attach / verify / detach under the driver lock
 *
 * The card detect edge reaches us through the dmhaman handler named by the
 * host's card_detect_handler key; it runs in interrupt context and only
 * posts a semaphore.
 */

/** Pending card detect edges the semaphore counts before posts are dropped. */
#define DMSDIOD_MAX_PENDING_EDGES   16u

/** Upper bound of debounce windows waited for a bouncing card detect pin. */
#define DMSDIOD_DEBOUNCE_ROUNDS     10

typedef struct
{
    const char*             host_path;  /**< /dev/dmsdioN */
    dmsdio_detect_config_t  config;     /**< Policy read from the host */
    dmosi_semaphore_t       edges;      /**< Posted by the card detect ISR */
    bool                    registered; /**< dmhaman handler registered */
} dmsdiod_t;

/* One open/ioctl/close per request: nothing stays open if the unit is stopped. */
static int host_ioctl(const dmsdiod_t* d, int command, void* arg)
{
    void* file = Dmod_FileOpen(d->host_path, "r");
    if (file == NULL)
    {
        return -ENODEV;
    }
    int ret = Dmod_Ioctl(file, command, arg);
    Dmod_FileClose(file);
    return ret;
}

/* dmhaman handler - interrupt context: never touches the driver. */
static int card_detect_edge(void* parameters, void* user_ctx)
{
    (void)parameters;
    dmsdiod_t* d = (dmsdiod_t*)user_ctx;
    return dmosi_semaphore_post(d->edges, 1);
}

/* Consume every pending edge. Returns true if there was at least one. */
static bool drain_edges(dmsdiod_t* d)
{
    bool any = false;
    while (dmosi_semaphore_wait(d->edges, 1, 0) == 0)
    {
        any = true;
    }
    return any;
}

/* After an edge: let the pin settle - one whole debounce window without a new edge. */
static void settle(dmsdiod_t* d)
{
    for (int round = 0; round < DMSDIOD_DEBOUNCE_ROUNDS; round++)
    {
        /* A removal aborts a running transfer right away, not after the rescan. */
        host_ioctl(d, dmsdio_ioctl_cmd_check_removal, NULL);
        dmosi_thread_sleep(d->config.debounce_ms);
        if (!drain_edges(d))
        {
            return;
        }
    }
}

static void rescan(dmsdiod_t* d)
{
    int ret = host_ioctl(d, dmsdio_ioctl_cmd_rescan, NULL);
    if (ret != 0 && ret != -ENODEV)
    {
        DMOD_LOG_WARN("dmsdiod: rescan of %s failed (%d)\n", d->host_path, ret);
    }
}

/*
 * dmosi_process_kill() (libsystemd stopping the unit) never returns to
 * main(), so the interrupt handler must go away here - it points into this
 * process's memory.
 */
static void release(dmsdiod_t* d)
{
    if (d->registered)
    {
        dmhaman_unregister_handler(d->config.card_detect_handler, card_detect_edge);
        d->registered = false;
    }
    if (d->edges != NULL)
    {
        dmosi_semaphore_destroy(d->edges);
        d->edges = NULL;
    }
}

static void service_exit(dmosi_process_t process, int exit_status, void* arg)
{
    (void)process;
    (void)exit_status;
    dmsdiod_t* d = (dmsdiod_t*)arg;
    release(d);
    Dmod_Free(d);
}

static int setup(dmsdiod_t* d)
{
    int ret = host_ioctl(d, dmsdio_ioctl_cmd_get_detect_config, &d->config);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmsdiod: %s is not a dmsdio host node (%d)\n", d->host_path, ret);
        return ret;
    }
    d->edges = dmosi_semaphore_create(0, DMSDIOD_MAX_PENDING_EDGES);
    if (d->edges == NULL)
    {
        return -ENOMEM;
    }
    if (d->config.card_detect_handler[0] != '\0')
    {
        ret = dmhaman_register_handler(d->config.card_detect_handler, card_detect_edge, d);
        if (ret != 0)
        {
            DMOD_LOG_ERROR("dmsdiod: cannot register card detect handler '%s' (%d)\n",
                           d->config.card_detect_handler, ret);
            return ret;
        }
        d->registered = true;
    }
    return 0;
}

static void monitor(dmsdiod_t* d)
{
    int32_t timeout = (d->config.poll_interval_ms != 0) ? (int32_t)d->config.poll_interval_ms : -1;
    for (;;)
    {
        if (dmosi_semaphore_wait(d->edges, 1, timeout) == 0)
        {
            settle(d);
        }
        rescan(d);
    }
}

static void print_usage(const char* prog)
{
    Dmod_Printf("Usage: %s <host_node>\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("SD card presence service for one dmsdio host node (e.g. /dev/dmsdio0).\n");
    Dmod_Printf("Normally started by libsystemd from dmsdiod@.ini for every host dmsdio\n");
    Dmod_Printf("reports under the \"sdio\" device class (see dmsdiod.rules).\n");
}

int main(int argc, char* argv[])
{
    if (argc < 2 || argv[1][0] == '\0')
    {
        print_usage(argv[0]);
        return -EINVAL;
    }
    dmsdiod_t* d = Dmod_Malloc(sizeof(*d));
    if (d == NULL)
    {
        return -ENOMEM;
    }
    d->host_path  = argv[1];
    d->edges      = NULL;
    d->registered = false;

    int ret = setup(d);
    /* Picks up the card detect pin dmdevfs linked after the driver was
     * created, and a card inserted or removed since. */
    if (ret == 0)
    {
        rescan(d);
    }
    if (ret != 0 || (!d->registered && d->config.poll_interval_ms == 0))
    {
        /* Failed, or nothing to wait for: presence is only ever re-checked
         * through dmsdio_ioctl_cmd_rescan by hand. */
        release(d);
        Dmod_Free(d);
        return ret;
    }
    if (dmosi_process_register_exit_callback(dmosi_process_current(), service_exit, d) == NULL)
    {
        DMOD_LOG_ERROR("dmsdiod: cannot register exit callback\n");
        release(d);
        Dmod_Free(d);
        return -ENOMEM;
    }
    DMOD_LOG_INFO("dmsdiod: monitoring %s (%s%s%s, poll %u ms)\n", d->host_path,
                  d->registered ? "card detect '" : "no card detect",
                  d->registered ? d->config.card_detect_handler : "",
                  d->registered ? "'" : "", (unsigned)d->config.poll_interval_ms);
    monitor(d);
    return 0;
}
