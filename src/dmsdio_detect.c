#include "dmsdio_internal.h"

/*
 * Card detect pin.
 *
 * Card detect is an optional dmgpio friend (friend_role=card_detect in the
 * same friend_group). The driver only samples it - during a scan and for
 * dmsdio_ioctl_cmd_check_removal. Waiting for its edges, debouncing and
 * periodic polling are the job of the dmsdiod service, which drives the
 * driver through the host node's rescan/check_removal ioctls.
 */

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

/*
 * Runs without the context lock: it has to get through while a transfer
 * holds it, so that transfer bails out with -ENODEV instead of running into
 * its timeouts. Only ever raises removal_pending; the next scan (under the
 * lock) settles the card state and clears it.
 */
int dmsdio_detect_check_removal(struct dmdrvi_context* ctx)
{
    bool present = true;
    int ret = dmsdio_detect_read_cd(ctx, &present);
    if (ret != 0)
    {
        return ret;
    }
    if (!present)
    {
        ctx->removal_pending = true;
        return -ENODEV;
    }
    return 0;
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
}

void dmsdio_detect_release(struct dmdrvi_context* ctx)
{
    if (ctx->cd_lock != NULL)
    {
        dmosi_mutex_lock(ctx->cd_lock);
        Dmod_Free(ctx->cd_path);
        ctx->cd_path = NULL;
        dmosi_mutex_unlock(ctx->cd_lock);
    }
}
