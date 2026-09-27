/**
 * @brief Minimal dmdrvi "device_available"/"device_unavailable" MAL
 *        implementation for dmsdio_test.c
 *
 * In production this MAL is implemented by dmdevfs (which turns each
 * announcement into a mounted device file). These tests exercise dmsdio.c
 * directly, with no dmdevfs anywhere in the picture, so this file plays
 * dmdevfs's part just enough to let dmsdio.c's own
 * dmdrvi_device_available()/_unavailable() calls resolve to *something*
 * instead of an unpatched MAL pointer - it only records the dev_num it was
 * given and whether the card is currently "available", which is all the
 * test steps in dmsdio_test.c need to construct their own dev_num for
 * dmdrvi_open() and to assert hot-plug worked.
 */
#define DMOD_ENABLE_REGISTRATION ON
#define DMOD_MAL_dmdrvi
#include "dmdrvi.h"

dmdrvi_dev_num_t g_fake_mal_last_dev_num;
bool             g_fake_mal_card_available = false;

void dmdrvi_device_available(dmdrvi_context_t context, const dmdrvi_dev_num_t *dev_num)
{
    (void)context;
    g_fake_mal_last_dev_num = *dev_num;
    g_fake_mal_card_available = true;
}

void dmdrvi_device_unavailable(dmdrvi_context_t context, const dmdrvi_dev_num_t *dev_num)
{
    (void)context;
    g_fake_mal_last_dev_num = *dev_num;
    g_fake_mal_card_available = false;
}
