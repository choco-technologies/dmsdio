#include "dmsdio_internal.h"
#include <string.h>

/*
 * Card presence worker.
 *
 * Card detect is an optional dmgpio friend (friend_role=card_detect in the
 * same friend_group). Its edge interrupt reaches us through a dmhaman
 * handler named by `card_detect_handler`; the handler runs in ISR context
 * and only enqueues an event. All bus work happens on the dmosi worker
 * thread below: it debounces the pin, then attaches or detaches the card
 * under the context lock.
 *
 * Without a card detect pin, presence is re-checked every poll_interval_ms
 * (CMD13 for an attached card, a full identification attempt otherwise).
 */

#define DEBOUNCE_MAX_ROUNDS     10

/* dmhaman handler - ISR context: never touches the bus, only enqueues. */
static int card_detect_isr(void* parameters, void* user_ctx)
{
    (void)parameters;
    struct dmdrvi_context* ctx = (struct dmdrvi_context*)user_ctx;
    if (ctx == NULL || ctx->magic != DMSDIO_CONTEXT_MAGIC || ctx->events == NULL)
    {
        return -EINVAL;
    }
    uint8_t event = (uint8_t)dmsdio_event_card_detect;
    return dmosi_queue_send(ctx->events, &event, 0);
}

int dmsdio_detect_read_cd(struct dmdrvi_context* ctx, bool* present)
{
    dmosi_mutex_lock(ctx->cd_lock);
    void* file = (ctx->cd_path != NULL) ? Dmod_FileOpen(ctx->cd_path, "r") : NULL;
    bool configured = (ctx->cd_path != NULL);
    dmosi_mutex_unlock(ctx->cd_lock);
    if (file == NULL)
    {
        return configured ? -EIO : -ENOENT;
    }
    dmgpio_pins_mask_t active = 0;
    int command = ctx->config.cd_active_high ? dmgpio_ioctl_cmd_get_high_pins_state
                                             : dmgpio_ioctl_cmd_get_low_pins_state;
    int ret = Dmod_Ioctl(file, command, &active);
    Dmod_FileClose(file);
    if (ret != 0)
    {
        return -EIO;
    }
    *present = (active != 0);
    return 0;
}

void dmsdio_detect_post(struct dmdrvi_context* ctx, dmsdio_event_t event)
{
    uint8_t value = (uint8_t)event;
    if (ctx->events != NULL && dmosi_queue_send(ctx->events, &value, 0) != 0)
    {
        DMOD_LOG_WARN("dmsdio%u: event queue full\n", (unsigned)ctx->config.major);
    }
}

void dmsdio_detect_set_cd_path(struct dmdrvi_context* ctx, const char* path)
{
    char* copy = (path != NULL) ? Dmod_StrDup(path) : NULL;
    if (path != NULL && copy == NULL)
    {
        DMOD_LOG_ERROR("dmsdio%u: cannot retain card detect path\n", (unsigned)ctx->config.major);
        return;
    }
    dmosi_mutex_lock(ctx->cd_lock);
    Dmod_Free(ctx->cd_path);
    ctx->cd_path = copy;
    dmosi_mutex_unlock(ctx->cd_lock);
    dmsdio_detect_post(ctx, dmsdio_event_card_detect);
}

/* Discard queued card detect edges. Returns true if a stop was requested. */
static bool drain_events(struct dmdrvi_context* ctx)
{
    uint8_t event;
    bool stop = false;
    while (dmosi_queue_receive(ctx->events, &event, 0) == 0)
    {
        stop = stop || (event == dmsdio_event_stop);
    }
    return stop;
}

/* Wait until two consecutive samples agree. Returns true if a stop was requested. */
static bool debounce(struct dmdrvi_context* ctx, bool* present, bool* valid)
{
    bool previous = false;
    *valid = (dmsdio_detect_read_cd(ctx, &previous) == 0);
    for (int round = 0; *valid && round < DEBOUNCE_MAX_ROUNDS; round++)
    {
        dmosi_thread_sleep(ctx->config.debounce_ms);
        if (drain_events(ctx))
        {
            return true;
        }
        bool current = false;
        *valid = (dmsdio_detect_read_cd(ctx, &current) == 0);
        if (*valid && current == previous)
        {
            break;
        }
        previous = current;
    }
    *present = previous;
    return false;
}

/* Handle a card detect edge. Returns true if a stop was requested. */
static bool handle_card_detect(struct dmdrvi_context* ctx)
{
    bool present = false;
    bool valid = false;
    if (debounce(ctx, &present, &valid))
    {
        return true;
    }
    if (!valid)
    {
        return false;   /* no (or unreadable) card detect pin: rely on scans */
    }
    ctx->removal_pending = !present;    /* lets a running transfer bail out early */
    dmsdio_lock(ctx);
    if (present)
    {
        /* A swap faster than the debounce window is caught here: the new
         * card does not answer CMD13 at the old RCA and is re-identified. */
        dmsdio_card_scan(ctx);
    }
    else
    {
        dmsdio_card_detach(ctx);
        ctx->scan_count++;
    }
    ctx->removal_pending = false;
    dmsdio_unlock(ctx);
    return false;
}

static void worker(void* arg)
{
    struct dmdrvi_context* ctx = (struct dmdrvi_context*)arg;
    bool stop = false;
    while (!stop)
    {
        uint32_t poll = ctx->config.poll_interval_ms;
        int32_t timeout = (poll != 0) ? (int32_t)poll : -1;
        uint8_t event = (uint8_t)dmsdio_event_scan;
        if (dmosi_queue_receive(ctx->events, &event, timeout) != 0)
        {
            event = (uint8_t)dmsdio_event_scan;
        }
        if (event == dmsdio_event_stop)
        {
            stop = true;
        }
        else if (event == dmsdio_event_card_detect)
        {
            stop = handle_card_detect(ctx);
        }
        else
        {
            dmsdio_lock(ctx);
            dmsdio_card_scan(ctx);
            dmsdio_unlock(ctx);
        }
    }
}

static int register_handler(struct dmdrvi_context* ctx)
{
    if (ctx->config.cd_handler_name == NULL)
    {
        return 0;
    }
    int ret = dmhaman_register_handler(ctx->config.cd_handler_name, card_detect_isr, ctx);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmsdio%u: cannot register card detect handler '%s'\n",
                       (unsigned)ctx->config.major, ctx->config.cd_handler_name);
        return -EBUSY;
    }
    ctx->cd_registered = true;
    return 0;
}

int dmsdio_detect_start(struct dmdrvi_context* ctx)
{
    ctx->events = dmosi_queue_create(sizeof(uint8_t), DMSDIO_EVENT_QUEUE_LENGTH);
    if (ctx->events == NULL)
    {
        return -ENOMEM;
    }
    int ret = register_handler(ctx);
    if (ret != 0)
    {
        return ret;
    }
    /* Own process, not whichever process happens to create the driver. */
    ctx->worker_process = dmosi_process_create("dmsdio", DMOD_MODULE_NAME, NULL);
    if (ctx->worker_process == NULL)
    {
        DMOD_LOG_ERROR("dmsdio%u: cannot create worker process\n", (unsigned)ctx->config.major);
        return -ENOMEM;
    }
    ctx->worker = dmosi_thread_create(worker, ctx, DMSDIO_WORKER_PRIORITY,
                                      DMSDIO_WORKER_STACK_SIZE, "dmsdio", ctx->worker_process);
    if (ctx->worker == NULL)
    {
        DMOD_LOG_ERROR("dmsdio%u: cannot start worker thread\n", (unsigned)ctx->config.major);
        return -ENOMEM;
    }
    dmsdio_detect_post(ctx, dmsdio_event_scan);
    return 0;
}

void dmsdio_detect_stop(struct dmdrvi_context* ctx)
{
    if (ctx->cd_registered)
    {
        dmhaman_unregister_handler(ctx->config.cd_handler_name, card_detect_isr);
        ctx->cd_registered = false;
    }
    if (ctx->worker != NULL)
    {
        uint8_t event = (uint8_t)dmsdio_event_stop;
        dmosi_queue_send(ctx->events, &event, -1);
        dmosi_thread_join(ctx->worker);
        dmosi_thread_destroy(ctx->worker);
        ctx->worker = NULL;
    }
    if (ctx->worker_process != NULL)
    {
        dmosi_process_destroy(ctx->worker_process);
        ctx->worker_process = NULL;
    }
    if (ctx->events != NULL)
    {
        dmosi_queue_destroy(ctx->events);
        ctx->events = NULL;
    }
    if (ctx->cd_lock != NULL)
    {
        dmosi_mutex_lock(ctx->cd_lock);
        Dmod_Free(ctx->cd_path);
        ctx->cd_path = NULL;
        dmosi_mutex_unlock(ctx->cd_lock);
    }
}
