/* Needed so this translation unit gets real storage for the dmdrvi DIF
 * signature string constants it looks up below - see dmdevfs.c/dmvfs.c
 * for the same pattern on every other real DIF consumer in the
 * ecosystem. */
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmsdio.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "mock_control.h"
#include "fake_mal.h"

#include <errno.h>
#include <string.h>

/* DMOD_USE_STDLIB=OFF builds don't link memcmp() - see dmdma_test_lease.c's
 * own parse_uint() for the same reasoning. */
static bool bytes_equal(const void *a, const void *b, size_t size)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < size; i++)
    {
        if (pa[i] != pb[i])
        {
            return false;
        }
    }
    return true;
}

/**
 * Mocked SD protocol tests - dmsdio.c is exercised exactly as any real
 * consumer (dmdevfs) would, through the dmdrvi DIF looked up by module
 * name (dmsdio genuinely is one of several dmdrvi implementers - dmuart,
 * dmspi, dmdma, ... - so unlike dmdma's lease API, this really is DIF's
 * 1:N territory). The x86_64 "dmsdio_port" bundled here is the mock in
 * src/port/x86_64/port.c, controlled through mock_control.h.
 */

static dmod_dmdrvi_create_t dmdrvi_create;
static dmod_dmdrvi_free_t   dmdrvi_free;
static dmod_dmdrvi_open_t   dmdrvi_open;
static dmod_dmdrvi_close_t  dmdrvi_close;
static dmod_dmdrvi_read_t   dmdrvi_read;
static dmod_dmdrvi_write_t  dmdrvi_write;
static dmod_dmdrvi_ioctl_t  dmdrvi_ioctl;
static dmod_dmdrvi_stat_t   dmdrvi_stat;

static dmdrvi_context_t g_context;

void dmod_test_setup(void)
{
    Dmod_Context_t *module = Dmod_GetModuleContext("dmsdio");

    dmdrvi_create = (dmod_dmdrvi_create_t)Dmod_GetDifFunction(module, dmod_dmdrvi_create_sig);
    dmdrvi_free   = (dmod_dmdrvi_free_t)Dmod_GetDifFunction(module, dmod_dmdrvi_free_sig);
    dmdrvi_open   = (dmod_dmdrvi_open_t)Dmod_GetDifFunction(module, dmod_dmdrvi_open_sig);
    dmdrvi_close  = (dmod_dmdrvi_close_t)Dmod_GetDifFunction(module, dmod_dmdrvi_close_sig);
    dmdrvi_read   = (dmod_dmdrvi_read_t)Dmod_GetDifFunction(module, dmod_dmdrvi_read_sig);
    dmdrvi_write  = (dmod_dmdrvi_write_t)Dmod_GetDifFunction(module, dmod_dmdrvi_write_sig);
    dmdrvi_ioctl  = (dmod_dmdrvi_ioctl_t)Dmod_GetDifFunction(module, dmod_dmdrvi_ioctl_sig);
    dmdrvi_stat   = (dmod_dmdrvi_stat_t)Dmod_GetDifFunction(module, dmod_dmdrvi_stat_sig);

    g_context = NULL;
    g_fake_mal_card_available = false;
}

void dmod_test_teardown(void)
{
    if (g_context != NULL)
    {
        dmdrvi_free(g_context);
        g_context = NULL;
    }
}

static dmini_context_t make_config(void)
{
    dmini_context_t ini = dmini_create();
    dmini_parse_string(ini,
        "[dmsdio]\n"
        "instance=0\n"
        "max_bus_width=4\n"
        "high_speed=1\n"
        "command_timeout_ms=100\n"
        "data_timeout_ms=100\n"
        "max_retries=3\n");
    return ini;
}

/**
 * @brief mock_reset() + dmdrvi_create() - the create() call itself runs
 *        the full identification sequence (see rescan_card() in dmsdio.c),
 *        so a successful return here already exercises CMD0..CMD7/SCR/etc.
 */
static dmdrvi_context_t create_with_card(dmsdio_card_type_t card_type, uint64_t capacity_blocks)
{
    dmsdio_port_mock_reset(card_type, capacity_blocks);

    dmini_context_t ini = make_config();
    dmdrvi_dev_num_t host_dev_num;
    dmdrvi_context_t context = dmdrvi_create(ini, &host_dev_num);
    dmini_destroy(ini);
    return context;
}

static void *open_card(dmdrvi_context_t context)
{
    /* dmsdio.c always announces the card sub-node as {major=instance,
     * minor=0} - see announce_card() - so the test does not need
     * g_fake_mal_last_dev_num for this, only for asserting hot-plug
     * actually fired. */
    dmdrvi_dev_num_t dev_num;
    dev_num.major = 0;
    dev_num.minor = 0;
    dev_num.flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_MINOR;
    return dmdrvi_open(context, 0, &dev_num);
}

/* ---- Card identification across every supported family ---- */

DMOD_TEST_STEP(identify_sdsc_v1)
{
    g_context = create_with_card(dmsdio_card_type_sdsc_v1, 8192);
    DMOD_TEST_EXPECT_NOT_NULL(g_context);
    DMOD_TEST_EXPECT_TRUE(g_fake_mal_card_available);

    void *handle = open_card(g_context);
    DMOD_TEST_EXPECT_NOT_NULL(handle);

    dmsdio_card_info_t info;
    DMOD_TEST_EXPECT_EQ(dmdrvi_ioctl(g_context, handle, dmsdio_ioctl_cmd_get_card_info, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.card_type, dmsdio_card_type_sdsc_v1);
    DMOD_TEST_EXPECT_EQ(info.csd.capacity_blocks, 8192ULL);
}

DMOD_TEST_STEP(identify_sdsc_v2)
{
    g_context = create_with_card(dmsdio_card_type_sdsc_v2, 8192);
    DMOD_TEST_EXPECT_NOT_NULL(g_context);

    void *handle = open_card(g_context);
    dmsdio_card_info_t info;
    DMOD_TEST_EXPECT_EQ(dmdrvi_ioctl(g_context, handle, dmsdio_ioctl_cmd_get_card_info, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.card_type, dmsdio_card_type_sdsc_v2);
}

DMOD_TEST_STEP(identify_sdhc)
{
    g_context = create_with_card(dmsdio_card_type_sdhc, 8000000); /* ~3.8 GiB */
    DMOD_TEST_EXPECT_NOT_NULL(g_context);

    void *handle = open_card(g_context);
    dmsdio_card_info_t info;
    DMOD_TEST_EXPECT_EQ(dmdrvi_ioctl(g_context, handle, dmsdio_ioctl_cmd_get_card_info, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.card_type, dmsdio_card_type_sdhc);
    DMOD_TEST_EXPECT_EQ(info.csd.capacity_blocks, 8000000ULL);
}

/**
 * @brief Capacity chosen so that capacity_blocks * 512 exceeds UINT32_MAX
 *        (2^32 - 1 = 4294967295) - if any layer between CSD parsing and
 *        dmdrvi_stat()/get_card_info truncated to 32 bits, this value
 *        would come back wrong.
 */
DMOD_TEST_STEP(identify_sdxc_reports_64bit_capacity_without_truncation)
{
    const uint64_t capacity_blocks = 4200000000ULL; /* ~2 TiB - exceeds UINT32_MAX bytes */
    const uint64_t capacity_bytes = capacity_blocks * DMSDIO_BLOCK_SIZE;
    DMOD_TEST_EXPECT_TRUE(capacity_bytes > 0xFFFFFFFFULL);

    g_context = create_with_card(dmsdio_card_type_sdhc /* refined to sdxc by capacity, see identify_card() */, capacity_blocks);
    DMOD_TEST_EXPECT_NOT_NULL(g_context);

    void *handle = open_card(g_context);
    dmsdio_card_info_t info;
    DMOD_TEST_EXPECT_EQ(dmdrvi_ioctl(g_context, handle, dmsdio_ioctl_cmd_get_card_info, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.card_type, dmsdio_card_type_sdxc);
    DMOD_TEST_EXPECT_EQ(info.csd.capacity_blocks, capacity_blocks);

    dmdrvi_stat_t stat;
    DMOD_TEST_EXPECT_EQ(dmdrvi_stat(g_context, "", &stat), 0);
    DMOD_TEST_EXPECT_EQ((uint64_t)stat.size, capacity_bytes);
}

/* ---- Block I/O ---- */

DMOD_TEST_STEP(read_write_roundtrip_full_sector)
{
    g_context = create_with_card(dmsdio_card_type_sdhc, 8192);
    void *handle = open_card(g_context);

    uint8_t write_buf[DMSDIO_BLOCK_SIZE];
    for (size_t i = 0; i < sizeof(write_buf); i++)
    {
        write_buf[i] = (uint8_t)(i * 3 + 7);
    }

    dmdrvi_ssize_t written = dmdrvi_write(g_context, handle, write_buf, sizeof(write_buf), (dmdrvi_offset_t)(DMSDIO_BLOCK_SIZE * 2));
    DMOD_TEST_EXPECT_EQ(written, (dmdrvi_ssize_t)sizeof(write_buf));

    uint8_t read_buf[DMSDIO_BLOCK_SIZE] = {0};
    dmdrvi_ssize_t read = dmdrvi_read(g_context, handle, read_buf, sizeof(read_buf), (dmdrvi_offset_t)(DMSDIO_BLOCK_SIZE * 2));
    DMOD_TEST_EXPECT_EQ(read, (dmdrvi_ssize_t)sizeof(read_buf));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(write_buf, read_buf, sizeof(write_buf)));
}

/**
 * @brief Writes a small, unaligned chunk spanning parts of two sectors -
 *        the surrounding, untouched bytes of both sectors must survive
 *        the read-modify-write exactly as they were.
 */
DMOD_TEST_STEP(partial_sector_read_modify_write_preserves_surrounding_bytes)
{
    g_context = create_with_card(dmsdio_card_type_sdhc, 8192);
    void *handle = open_card(g_context);

    /* Seed two full sectors with a known pattern first. */
    uint8_t seed[DMSDIO_BLOCK_SIZE * 2];
    for (size_t i = 0; i < sizeof(seed); i++)
    {
        seed[i] = (uint8_t)i;
    }
    DMOD_TEST_EXPECT_EQ(dmdrvi_write(g_context, handle, seed, sizeof(seed), 0), (dmdrvi_ssize_t)sizeof(seed));

    /* Overwrite 10 bytes straddling the sector boundary (offset 508..517). */
    uint8_t patch[10];
    memset(patch, 0xAA, sizeof(patch));
    dmdrvi_offset_t patch_offset = DMSDIO_BLOCK_SIZE - 5;
    DMOD_TEST_EXPECT_EQ(dmdrvi_write(g_context, handle, patch, sizeof(patch), patch_offset), (dmdrvi_ssize_t)sizeof(patch));

    uint8_t verify[DMSDIO_BLOCK_SIZE * 2];
    DMOD_TEST_EXPECT_EQ(dmdrvi_read(g_context, handle, verify, sizeof(verify), 0), (dmdrvi_ssize_t)sizeof(verify));

    /* Untouched prefix/suffix must be byte-for-byte the original seed. */
    DMOD_TEST_EXPECT_TRUE(bytes_equal(verify, seed, (size_t)patch_offset));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(verify + patch_offset, patch, sizeof(patch)));
    size_t after = (size_t)patch_offset + sizeof(patch);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(verify + after, seed + after, sizeof(seed) - after));
}

/* ---- Error handling ---- */

DMOD_TEST_STEP(transient_crc_error_recovered_by_retry)
{
    g_context = create_with_card(dmsdio_card_type_sdhc, 8192);
    void *handle = open_card(g_context);

    /* max_retries=3 in make_config() - 2 failures must still succeed. */
    dmsdio_port_mock_inject_error(dmsdio_error_command_crc, 2);

    uint8_t buf[DMSDIO_BLOCK_SIZE];
    dmdrvi_ssize_t result = dmdrvi_read(g_context, handle, buf, sizeof(buf), 0);
    DMOD_TEST_EXPECT_EQ(result, (dmdrvi_ssize_t)sizeof(buf));
}

DMOD_TEST_STEP(persistent_crc_error_exhausts_retries)
{
    g_context = create_with_card(dmsdio_card_type_sdhc, 8192);
    void *handle = open_card(g_context);

    /* Far more failures than max_retries=3 can absorb. */
    dmsdio_port_mock_inject_error(dmsdio_error_command_crc, 1000);

    uint8_t buf[DMSDIO_BLOCK_SIZE];
    dmdrvi_ssize_t result = dmdrvi_read(g_context, handle, buf, sizeof(buf), 0);
    DMOD_TEST_EXPECT_EQ(result, -EIO);
}

DMOD_TEST_STEP(timeout_during_identification_reports_no_card)
{
    dmsdio_port_mock_reset(dmsdio_card_type_sdsc_v1, 8192);
    dmsdio_port_mock_inject_error(dmsdio_error_no_response, 1000);

    dmini_context_t ini = make_config();
    dmdrvi_dev_num_t host_dev_num;
    g_context = dmdrvi_create(ini, &host_dev_num);
    dmini_destroy(ini);

    /* create() itself must still succeed (the host controller exists even
     * with no usable card in the slot) - only the card sub-node must never
     * have been announced. */
    DMOD_TEST_EXPECT_NOT_NULL(g_context);
    DMOD_TEST_EXPECT_FALSE(g_fake_mal_card_available);
}

DMOD_TEST_STEP(removal_during_transfer_reported_precisely)
{
    g_context = create_with_card(dmsdio_card_type_sdhc, 8192);
    void *handle = open_card(g_context);
    DMOD_TEST_EXPECT_TRUE(g_fake_mal_card_available);

    dmsdio_port_mock_set_removed(true);

    uint8_t buf[DMSDIO_BLOCK_SIZE];
    dmdrvi_ssize_t result = dmdrvi_read(g_context, handle, buf, sizeof(buf), 0);
    DMOD_TEST_EXPECT_EQ(result, -ENODEV);
    DMOD_TEST_EXPECT_FALSE(g_fake_mal_card_available);
}

/**
 * @brief dmsdio_ioctl_cmd_get_generation reflects the generation an
 *        already-open handle was captured against
 *
 * Full hot-swap (remove card A, insert a *different* card B without the
 * host controller itself being recreated, old handle must be rejected) is
 * exactly what dmsdio_generation_t exists for, but actually triggering a
 * second identify_card() pass needs the GPIO-backed card-detect friend
 * (see dmsdio.c's rescan_card()/hotplug_worker()) - not something this
 * protocol-only mock reaches into. That path is covered on real hardware
 * (see docs/README.md "Hot-plug detection" testing notes) by physically
 * swapping the card; this step only checks the generation plumbing a
 * handle actually carries is correct.
 */
DMOD_TEST_STEP(handle_generation_matches_card_generation)
{
    g_context = create_with_card(dmsdio_card_type_sdhc, 8192);
    void *handle = open_card(g_context);
    DMOD_TEST_EXPECT_NOT_NULL(handle);

    dmsdio_generation_t generation = 0;
    DMOD_TEST_EXPECT_EQ(dmdrvi_ioctl(g_context, handle, dmsdio_ioctl_cmd_get_generation, &generation), 0);
    DMOD_TEST_EXPECT_NE(generation, 0U);

    dmsdio_card_info_t info;
    DMOD_TEST_EXPECT_EQ(dmdrvi_ioctl(g_context, handle, dmsdio_ioctl_cmd_get_card_info, &info), 0);
    DMOD_TEST_EXPECT_EQ(info.generation, generation);
}
