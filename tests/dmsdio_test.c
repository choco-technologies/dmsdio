#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmsdio.h"

static dmsdio_t g_handle = NULL;

void dmod_test_setup(void)
{
    g_handle = dmsdio_create();
}

void dmod_test_teardown(void)
{
    dmsdio_destroy(g_handle);
    g_handle = NULL;
}

DMOD_TEST_STEP(dmsdio_create)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_handle);
}

DMOD_TEST_STEP(dmsdio_is_valid)
{
    DMOD_TEST_EXPECT_TRUE(dmsdio_is_valid(g_handle));
}

DMOD_TEST_STEP(dmsdio_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmsdio_destroy(NULL);
}
