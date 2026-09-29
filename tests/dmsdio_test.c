#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmsdio.h"
#include "dmsdio_mock.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>

/*
 * Mocked protocol tests.
 *
 * The x86_64 dmsdio_port simulates an SD card at the command level. The
 * driver is used exactly the way dmdevfs uses it: its dmdrvi DIF functions
 * are looked up at runtime by module name, and this test module implements
 * the dmdrvi MAL (dmdrvi_device_available/_unavailable) in place of dmdevfs
 * to observe hot-plug notifications, and calls the monitor ioctls itself in
 * place of dmdevmon.
 */

#define HOST            1
#define GIB             (1024ull * 1024ull * 1024ull)
#define BLK             512u

typedef struct
{
    dmod_dmdrvi_create_t    create;
    dmod_dmdrvi_free_t      free;
    dmod_dmdrvi_open_t      open;
    dmod_dmdrvi_close_t     close;
    dmod_dmdrvi_read_t      read;
    dmod_dmdrvi_write_t     write;
    dmod_dmdrvi_ioctl_t     ioctl;
    dmod_dmdrvi_flush_t     flush;
    dmod_dmdrvi_stat_t      stat;
    dmod_dmdrvi_path_ready_t path_ready;
} driver_t;

static driver_t          g_drv;
static dmdrvi_context_t  g_ctx;
static dmdrvi_dev_num_t  g_host_num;
static void*             g_host;
static void*             g_card;
static volatile int      g_available;
static volatile int      g_unavailable;
static dmdrvi_dev_num_t  g_announced;
/* Aligned for direct transfers; g_buf + 1 is the misaligned case. */
static uint8_t           g_buf[16 * BLK + 8] __attribute__((aligned(DMSDIO_TRANSFER_ALIGNMENT)));
static uint8_t           g_ref[16 * BLK] __attribute__((aligned(DMSDIO_TRANSFER_ALIGNMENT)));

/* ---- dmdrvi MAL (normally implemented by dmdevfs) ---- */

void dmdrvi_device_available(dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num)
{
    (void)context;
    g_announced = *dev_num;
    g_available++;
}

void dmdrvi_device_unavailable(dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num)
{
    (void)context;
    (void)dev_num;
    g_unavailable++;
}

/* ---- harness ---- */

static bool load_driver(void)
{
    Dmod_Context_t* module = Dmod_GetModuleContext("dmsdio");
    if (module == NULL)
    {
        return false;
    }
    g_drv.create = Dmod_GetDifFunction(module, dmod_dmdrvi_create_sig);
    g_drv.free   = Dmod_GetDifFunction(module, dmod_dmdrvi_free_sig);
    g_drv.open   = Dmod_GetDifFunction(module, dmod_dmdrvi_open_sig);
    g_drv.close  = Dmod_GetDifFunction(module, dmod_dmdrvi_close_sig);
    g_drv.read   = Dmod_GetDifFunction(module, dmod_dmdrvi_read_sig);
    g_drv.write  = Dmod_GetDifFunction(module, dmod_dmdrvi_write_sig);
    g_drv.ioctl  = Dmod_GetDifFunction(module, dmod_dmdrvi_ioctl_sig);
    g_drv.flush  = Dmod_GetDifFunction(module, dmod_dmdrvi_flush_sig);
    g_drv.stat   = Dmod_GetDifFunction(module, dmod_dmdrvi_stat_sig);
    g_drv.path_ready = Dmod_GetDifFunction(module, dmod_dmdrvi_path_ready_sig);
    return g_drv.create && g_drv.free && g_drv.open && g_drv.close && g_drv.read &&
           g_drv.write && g_drv.ioctl && g_drv.flush && g_drv.stat && g_drv.path_ready;
}

static dmdrvi_dev_num_t card_num(void)
{
    dmdrvi_dev_num_t num;
    memset(&num, 0, sizeof(num));
    num.flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_MINOR;
    num.major = g_host_num.major;
    num.minor = 0;
    return num;
}

static dmsdio_host_info_t host_info(void)
{
    dmsdio_host_info_t info;
    memset(&info, 0, sizeof(info));
    g_drv.ioctl(g_ctx, g_host, dmsdio_ioctl_cmd_get_host_info, &info);
    return info;
}

static dmsdio_card_info_t card_info(void)
{
    dmsdio_card_info_t info;
    memset(&info, 0, sizeof(info));
    g_drv.ioctl(g_ctx, g_host, dmsdio_ioctl_cmd_get_card_info, &info);
    return info;
}

static void open_card(void)
{
    dmdrvi_dev_num_t num = card_num();
    if (g_card != NULL)
    {
        g_drv.close(g_ctx, g_card);
    }
    g_card = g_drv.open(g_ctx, DMDRVI_O_RDWR, &num);
}

static void stop_driver(void)
{
    if (g_ctx == NULL)
    {
        return;
    }
    if (g_card != NULL)
    {
        g_drv.close(g_ctx, g_card);
    }
    if (g_host != NULL)
    {
        g_drv.close(g_ctx, g_host);
    }
    g_drv.free(g_ctx);
    g_ctx = NULL;
    g_card = NULL;
    g_host = NULL;
}

/* (Re)create the driver context with extra ini lines, card type already inserted. */
static void start_driver(const char* extra)
{
    char ini_text[512];
    stop_driver();
    Dmod_SnPrintf(ini_text, sizeof(ini_text),
                  "[dmsdio]\ninstance=%d\nbus_width=4\npoll_interval_ms=0\nretries=2\n%s",
                  HOST, extra != NULL ? extra : "");
    dmini_context_t ini = dmini_create();
    dmini_parse_string(ini, ini_text);
    g_ctx = g_drv.create(ini, &g_host_num);
    dmini_destroy(ini);
    if (g_ctx == NULL)
    {
        return;
    }
    g_host = g_drv.open(g_ctx, DMDRVI_O_RDWR, &g_host_num);
    /* What dmdevfs does once the host node is registered and mounted... */
    g_drv.path_ready(g_ctx, &g_host_num, "/dev/dmsdio0");
    /* ...and what dmdevmon does first when dmdevfs reported it. */
    g_drv.ioctl(g_ctx, g_host, DMDRVI_IOCTL_MONITOR_REFRESH, NULL);
    open_card();
}

static int refresh(void)
{
    return g_drv.ioctl(g_ctx, g_host, DMDRVI_IOCTL_MONITOR_REFRESH, NULL);
}

static int monitor_event(void)
{
    return g_drv.ioctl(g_ctx, g_host, DMDRVI_IOCTL_MONITOR_EVENT, NULL);
}

/* Swap the inserted card for a fresh one of another type. */
static void use_card(dmsdio_card_type_t type, const char* extra)
{
    dmsdio_port_mock_insert(HOST, type);
    start_driver(extra);
}

static dmdrvi_ssize_t card_read(void* buf, size_t size, uint64_t offset)
{
    return g_drv.read(g_ctx, g_card, buf, size, (dmdrvi_offset_t)offset);
}

static dmdrvi_ssize_t card_write(const void* buf, size_t size, uint64_t offset)
{
    return g_drv.write(g_ctx, g_card, buf, size, (dmdrvi_offset_t)offset);
}

static bool matches_pattern(const uint8_t* data, uint64_t offset, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        uint64_t at = offset + i;
        if (data[i] != dmsdio_mock_pattern(at / BLK, (uint32_t)(at % BLK)))
        {
            return false;
        }
    }
    return true;
}

static bool bytes_equal(const uint8_t* a, const uint8_t* b, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

static void fill(uint8_t* data, size_t size, uint8_t seed)
{
    for (size_t i = 0; i < size; i++)
    {
        data[i] = (uint8_t)(seed + i * 13u);
    }
}

static dmsdio_mock_stats_t stats(bool reset)
{
    dmsdio_mock_stats_t s;
    memset(&s, 0, sizeof(s));
    dmsdio_port_mock_get_stats(HOST, &s, reset);
    return s;
}

void dmod_test_setup(void)
{
    static bool loaded = false;
    if (!loaded)
    {
        loaded = load_driver();
    }
    g_available = 0;
    g_unavailable = 0;
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_none, 0, 0);
    dmsdio_port_mock_set_slow_writes(HOST, false);
    use_card(dmsdio_card_type_sdhc, NULL);
    stats(true);
}

void dmod_test_teardown(void)
{
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_none, 0, 0);
    stop_driver();
}

/* ======================================================================
 *  Identification
 * ====================================================================== */

DMOD_TEST_STEP(dmsdio_driver_is_discoverable_through_dmdrvi)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_drv.create);
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
    DMOD_TEST_EXPECT_NOT_NULL(g_host);
    DMOD_TEST_EXPECT_EQ(g_host_num.flags, DMDRVI_NUM_MAJOR);
    DMOD_TEST_EXPECT_EQ(g_host_num.major, 0);
}

DMOD_TEST_STEP(dmsdio_announces_card_node)
{
    DMOD_TEST_EXPECT_EQ(g_available, 1);
    DMOD_TEST_EXPECT_EQ(g_announced.flags, DMDRVI_NUM_MAJOR | DMDRVI_NUM_MINOR);
    DMOD_TEST_EXPECT_EQ(g_announced.major, 0);
    DMOD_TEST_EXPECT_EQ(g_announced.minor, 0);
    DMOD_TEST_EXPECT_NOT_NULL(g_card);
}

DMOD_TEST_STEP(dmsdio_card_node_waits_for_host_registration)
{
    stop_driver();
    g_available = 0;
    dmini_context_t ini = dmini_create();
    dmini_parse_string(ini, "[dmsdio]\ninstance=1\npoll_interval_ms=0\n");
    g_ctx = g_drv.create(ini, &g_host_num);
    dmini_destroy(ini);
    g_host = g_drv.open(g_ctx, DMDRVI_O_RDWR, &g_host_num);
    /* identified, but dmdevfs has not registered the host node yet */
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_TRUE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(g_available, 0);
    dmdrvi_dev_num_t num = card_num();
    g_drv.path_ready(g_ctx, &num, "/dev/dmsdio0/0");        /* card node: ignored */
    g_drv.path_ready(g_ctx, &g_host_num, "/dev/dmsdio0");
    DMOD_TEST_EXPECT_EQ(g_available, 0);                    /* only REFRESH announces */
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_EQ(g_available, 1);
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_EQ(g_available, 1);                    /* announced once */
}

DMOD_TEST_STEP(dmsdio_identification_retries_transient_errors)
{
    dmsdio_port_mock_remove(HOST);
    start_driver(NULL);
    dmsdio_port_mock_insert(HOST, dmsdio_card_type_sdhc);
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_cmd_crc, 2, 1);   /* CMD2 */
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_EQ(stats(false).faults_injected, 1u);
    DMOD_TEST_EXPECT_EQ(card_info().type, dmsdio_card_type_sdhc);
}

DMOD_TEST_STEP(dmsdio_identifies_sdsc_v1)
{
    use_card(dmsdio_card_type_sdsc_v1, NULL);
    dmsdio_card_info_t info = card_info();
    dmsdio_mock_stats_t s = stats(false);
    DMOD_TEST_EXPECT_EQ(info.type, dmsdio_card_type_sdsc_v1);
    DMOD_TEST_EXPECT_FALSE(info.block_addressing);
    DMOD_TEST_EXPECT_EQ(info.capacity_bytes, 1ull * GIB);
    DMOD_TEST_EXPECT_EQ(info.csd.structure, 0);
    DMOD_TEST_EXPECT_EQ(info.bus_width, dmsdio_bus_width_4bit);
    DMOD_TEST_EXPECT_FALSE(info.high_speed);        /* SD 1.x has no CMD6 */
    DMOD_TEST_EXPECT_EQ(info.clock_hz, 24000000u);
    DMOD_TEST_EXPECT_EQ(s.commands[16], 1u);        /* SET_BLOCKLEN for SDSC */
    DMOD_TEST_EXPECT_EQ(s.commands[6], 0u);
}

DMOD_TEST_STEP(dmsdio_identifies_sdsc_v2)
{
    use_card(dmsdio_card_type_sdsc_v2, NULL);
    dmsdio_card_info_t info = card_info();
    DMOD_TEST_EXPECT_EQ(info.type, dmsdio_card_type_sdsc_v2);
    DMOD_TEST_EXPECT_FALSE(info.block_addressing);
    DMOD_TEST_EXPECT_EQ(info.capacity_bytes, 2ull * GIB);
    DMOD_TEST_EXPECT_EQ(info.csd.read_bl_len, 10);
    DMOD_TEST_EXPECT_TRUE(info.high_speed);
    DMOD_TEST_EXPECT_EQ(info.clock_hz, 48000000u);
    DMOD_TEST_EXPECT_EQ(info.cid.manufacturer_id, 0x03);
    DMOD_TEST_EXPECT_EQ(strcmp(info.cid.oem_id, "SD"), 0);
    DMOD_TEST_EXPECT_EQ(info.cid.manufacture_year, 2024);
}

DMOD_TEST_STEP(dmsdio_identifies_sdhc)
{
    dmsdio_card_info_t info = card_info();
    dmsdio_mock_stats_t s = stats(false);
    DMOD_TEST_EXPECT_EQ(info.type, dmsdio_card_type_sdhc);
    DMOD_TEST_EXPECT_TRUE(info.block_addressing);
    DMOD_TEST_EXPECT_EQ(info.capacity_bytes, 8ull * GIB);
    DMOD_TEST_EXPECT_EQ(info.csd.structure, 1);
    DMOD_TEST_EXPECT_TRUE(info.high_speed);
    DMOD_TEST_EXPECT_TRUE(s.card_high_speed);
    DMOD_TEST_EXPECT_TRUE(s.card_bus_4bit);
    DMOD_TEST_EXPECT_EQ(s.bus_width, dmsdio_bus_width_4bit);
    DMOD_TEST_EXPECT_EQ(info.ssr.au_size_bytes, 4u * 1024u * 1024u);
    DMOD_TEST_EXPECT_EQ(info.generation, host_info().generation);
}

DMOD_TEST_STEP(dmsdio_identifies_sdxc_with_full_64bit_capacity)
{
    use_card(dmsdio_card_type_sdxc, NULL);
    dmsdio_card_info_t info = card_info();
    dmdrvi_block_info_t block;
    dmdrvi_stat_t st;
    DMOD_TEST_EXPECT_EQ(info.type, dmsdio_card_type_sdxc);
    DMOD_TEST_EXPECT_EQ(info.capacity_bytes, 64ull * GIB);
    DMOD_TEST_EXPECT_TRUE(info.scr.sd_spec3);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_BLOCK_GET_INFO, &block), 0);
    DMOD_TEST_EXPECT_EQ(block.logical_block_size, BLK);
    DMOD_TEST_EXPECT_EQ(block.block_count, 64ull * GIB / BLK);
    DMOD_TEST_EXPECT_TRUE((block.flags & DMDRVI_BLOCK_FLAG_REMOVABLE) != 0);
    DMOD_TEST_EXPECT_TRUE((block.flags & DMDRVI_BLOCK_FLAG_DISCARD_SUPPORTED) != 0);
    DMOD_TEST_EXPECT_EQ(g_drv.stat(g_ctx, "/dev/dmsdio0/0", &st), 0);
    DMOD_TEST_EXPECT_EQ(st.size, 64ull * GIB);
    DMOD_TEST_EXPECT_EQ(g_drv.stat(g_ctx, "/dev/dmsdio0", &st), 0);
    DMOD_TEST_EXPECT_EQ(st.size, 0u);
}

DMOD_TEST_STEP(dmsdio_one_bit_bus_when_configured)
{
    use_card(dmsdio_card_type_sdhc, "bus_width=1\n");
    dmsdio_mock_stats_t s = stats(false);
    DMOD_TEST_EXPECT_EQ(card_info().bus_width, dmsdio_bus_width_1bit);
    DMOD_TEST_EXPECT_FALSE(s.card_bus_4bit);
    DMOD_TEST_EXPECT_EQ(s.app_commands[6], 0u);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
}

DMOD_TEST_STEP(dmsdio_default_speed_when_high_speed_refused)
{
    dmsdio_port_mock_insert(HOST, dmsdio_card_type_sdhc);
    dmsdio_port_mock_refuse_high_speed(HOST, true);
    start_driver(NULL);
    dmsdio_card_info_t info = card_info();
    DMOD_TEST_EXPECT_EQ(info.type, dmsdio_card_type_sdhc);
    DMOD_TEST_EXPECT_FALSE(info.high_speed);
    DMOD_TEST_EXPECT_EQ(info.clock_hz, 24000000u);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
}

DMOD_TEST_STEP(dmsdio_clock_limit_is_respected)
{
    use_card(dmsdio_card_type_sdhc, "max_clock_hz=12000000\n");
    dmsdio_mock_stats_t s = stats(false);
    DMOD_TEST_EXPECT_FALSE(card_info().high_speed);
    DMOD_TEST_EXPECT_TRUE(s.max_clock_hz <= 12000000u);
}

DMOD_TEST_STEP(dmsdio_no_card_is_reported)
{
    dmsdio_port_mock_remove(HOST);
    start_driver(NULL);
    g_available = 0;
    dmdrvi_stat_t st;
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
    DMOD_TEST_EXPECT_NULL(g_card);
    DMOD_TEST_EXPECT_FALSE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(refresh(), -ENODEV);
    DMOD_TEST_EXPECT_EQ(g_drv.stat(g_ctx, "/dev/dmsdio0/0", &st), -ENODEV);
    DMOD_TEST_EXPECT_EQ(g_available, 0);
    DMOD_TEST_EXPECT_EQ(host_info().last_error, 0);     /* an empty slot is not an error */
    DMOD_TEST_EXPECT_FALSE(stats(false).powered);
}

DMOD_TEST_STEP(dmsdio_malformed_identification_response_is_eproto)
{
    dmsdio_port_mock_remove(HOST);
    start_driver(NULL);
    dmsdio_port_mock_insert(HOST, dmsdio_card_type_sdhc);
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_bad_index, 8, 1);
    DMOD_TEST_EXPECT_EQ(refresh(), -EPROTO);
    DMOD_TEST_EXPECT_EQ(host_info().last_error, -EPROTO);
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
}

DMOD_TEST_STEP(dmsdio_reads_active_board_section)
{
    stop_driver();
    dmini_context_t ini = dmini_create();
    dmini_parse_string(ini, "[sd_card_detect]\ninstance=9\nmode=input\n"
                            "[sd_card]\ninstance=1\nmajor=3\npoll_interval_ms=0\n");
    dmini_set_active_section(ini, "sd_card", 0);
    g_ctx = g_drv.create(ini, &g_host_num);
    dmini_destroy(ini);
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
    DMOD_TEST_EXPECT_EQ(g_host_num.major, 3);
    g_host = g_drv.open(g_ctx, DMDRVI_O_RDWR, &g_host_num);
    g_drv.path_ready(g_ctx, &g_host_num, "/dev/dmsdio3");
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_EQ(host_info().instance, 1);
    DMOD_TEST_EXPECT_TRUE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(g_announced.major, 3);
}

DMOD_TEST_STEP(dmsdio_rejects_invalid_configuration)
{
    stop_driver();
    dmini_context_t ini = dmini_create();
    dmini_parse_string(ini, "[dmsdio]\ninstance=1\nbus_width=8\n");
    dmdrvi_dev_num_t num;
    DMOD_TEST_EXPECT_NULL(g_drv.create(ini, &num));
    dmini_destroy(ini);

    ini = dmini_create();
    dmini_parse_string(ini, "[dmsdio]\ninstance=7\nbus_width=4\n");
    DMOD_TEST_EXPECT_NULL(g_drv.create(ini, &num));     /* host acquisition fails */
    dmini_destroy(ini);

    ini = dmini_create();
    dmini_parse_string(ini, "[dmsdio]\ninstance=1\n"
                            "monitor_event_handler=a_handler_name_longer_than_31_chars\n");
    DMOD_TEST_EXPECT_NULL(g_drv.create(ini, &num));
    dmini_destroy(ini);
}

/* ======================================================================
 *  Data path
 * ====================================================================== */

DMOD_TEST_STEP(dmsdio_full_sector_round_trip_uses_multi_block)
{
    fill(g_ref, 8 * BLK, 0x11);
    DMOD_TEST_EXPECT_EQ(card_write(g_ref, 8 * BLK, 4096), (dmdrvi_ssize_t)(8 * BLK));
    memset(g_buf, 0, sizeof(g_buf));
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 8 * BLK, 4096), (dmdrvi_ssize_t)(8 * BLK));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_buf, g_ref, 8 * BLK));
    dmsdio_mock_stats_t s = stats(false);
    DMOD_TEST_EXPECT_EQ(s.commands[25], 1u);
    DMOD_TEST_EXPECT_EQ(s.commands[18], 1u);
    DMOD_TEST_EXPECT_EQ(s.commands[12], 2u);
    DMOD_TEST_EXPECT_EQ(s.blocks_written, 8u);
    DMOD_TEST_EXPECT_EQ(g_drv.flush(g_ctx, g_card), 0);
}

DMOD_TEST_STEP(dmsdio_large_request_is_split)
{
    use_card(dmsdio_card_type_sdhc, "max_blocks_per_transfer=4\n");
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 10 * BLK, 0), (dmdrvi_ssize_t)(10 * BLK));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf, 0, 10 * BLK));
    dmsdio_mock_stats_t s = stats(false);
    DMOD_TEST_EXPECT_EQ(s.commands[18], 3u);    /* 4 + 4 + 2 blocks */
}

DMOD_TEST_STEP(dmsdio_unaligned_write_preserves_neighbours)
{
    const uint64_t offset = 1000;               /* spans blocks 1 and 2 */
    fill(g_ref, 100, 0x5A);
    DMOD_TEST_EXPECT_EQ(card_write(g_ref, 100, offset), 100);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 4 * BLK, 0), (dmdrvi_ssize_t)(4 * BLK));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf, 0, offset));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_buf + offset, g_ref, 100));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf + offset + 100, offset + 100, 4 * BLK - offset - 100));
}

DMOD_TEST_STEP(dmsdio_unaligned_read_spanning_blocks)
{
    const uint64_t offset = 3 * BLK + 17;
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 3 * BLK, offset), (dmdrvi_ssize_t)(3 * BLK));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf, offset, 3 * BLK));
    /* misaligned destination buffer takes the bounce path */
    DMOD_TEST_EXPECT_EQ(card_read(g_buf + 1, 2 * BLK, 0), (dmdrvi_ssize_t)(2 * BLK));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf + 1, 0, 2 * BLK));
}

DMOD_TEST_STEP(dmsdio_misaligned_buffer_bounces_multi_block)
{
    /* 16 blocks through the 8-block bounce buffer: two commands, not 16. */
    DMOD_TEST_EXPECT_EQ(card_read(g_buf + 1, 16 * BLK, 0), (dmdrvi_ssize_t)(16 * BLK));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf + 1, 0, 16 * BLK));
    dmsdio_mock_stats_t s = stats(true);
    DMOD_TEST_EXPECT_EQ(s.commands[18], 2u);
    DMOD_TEST_EXPECT_EQ(s.commands[17], 0u);

    fill(g_buf + 1, 16 * BLK, 0x37);
    DMOD_TEST_EXPECT_EQ(card_write(g_buf + 1, 16 * BLK, 32 * BLK), (dmdrvi_ssize_t)(16 * BLK));
    s = stats(true);
    DMOD_TEST_EXPECT_EQ(s.commands[25], 2u);
    DMOD_TEST_EXPECT_EQ(s.commands[24], 0u);
    DMOD_TEST_EXPECT_EQ(card_read(g_ref, 16 * BLK, 32 * BLK), (dmdrvi_ssize_t)(16 * BLK));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_ref, g_buf + 1, 16 * BLK));
}

DMOD_TEST_STEP(dmsdio_writes_from_slow_memory_go_through_the_port_buffer)
{
    /* Like STM32F7 with the buffer in external SDRAM: the port only takes
     * its own buffer for writes, reads stay direct. */
    dmsdio_port_mock_set_slow_writes(HOST, true);
    fill(g_buf, 16 * BLK, 0x21);
    DMOD_TEST_EXPECT_EQ(card_write(g_buf, 16 * BLK, 64 * BLK), (dmdrvi_ssize_t)(16 * BLK));
    fill(g_ref, 100, 0x5A);
    DMOD_TEST_EXPECT_EQ(card_write(g_ref, 100, 64 * BLK + 100), 100);    /* inside one block */
    dmsdio_mock_stats_t s = stats(true);
    DMOD_TEST_EXPECT_EQ(s.indirect_writes, 0u);
    DMOD_TEST_EXPECT_EQ(s.commands[25], 2u);                /* 8 + 8 blocks */
    DMOD_TEST_EXPECT_EQ(s.blocks_written, 17u);             /* + the partial block */

    DMOD_TEST_EXPECT_EQ(card_read(g_buf + BLK, 8 * BLK, 64 * BLK), (dmdrvi_ssize_t)(8 * BLK));
    s = stats(true);
    DMOD_TEST_EXPECT_EQ(s.commands[18], 1u);                /* reads stay direct */
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_buf + BLK + 100, g_ref, 100));
    dmsdio_port_mock_set_slow_writes(HOST, false);
}

DMOD_TEST_STEP(dmsdio_bounce_blocks_is_configurable)
{
    use_card(dmsdio_card_type_sdhc, "bounce_blocks=4\n");
    DMOD_TEST_EXPECT_NOT_NULL(g_ctx);
    stats(true);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf + 1, 16 * BLK, 0), (dmdrvi_ssize_t)(16 * BLK));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf + 1, 0, 16 * BLK));
    DMOD_TEST_EXPECT_EQ(stats(false).commands[18], 4u);

    use_card(dmsdio_card_type_sdhc, "bounce_blocks=0\n");
    DMOD_TEST_EXPECT_NULL(g_ctx);
}

DMOD_TEST_STEP(dmsdio_offsets_above_4gib_are_not_truncated)
{
    use_card(dmsdio_card_type_sdxc, NULL);
    const uint64_t offset = 5ull * GIB + 3 * BLK + 123;
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 2 * BLK, offset), (dmdrvi_ssize_t)(2 * BLK));
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf, offset, 2 * BLK));

    const uint64_t last = 64ull * GIB - BLK;
    fill(g_ref, BLK, 0x77);
    DMOD_TEST_EXPECT_EQ(card_write(g_ref, BLK, last), (dmdrvi_ssize_t)BLK);
    DMOD_TEST_EXPECT_EQ(stats(false).last_address, last / BLK);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, last), (dmdrvi_ssize_t)BLK);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_buf, g_ref, BLK));
}

DMOD_TEST_STEP(dmsdio_sdsc_uses_byte_addresses)
{
    use_card(dmsdio_card_type_sdsc_v2, NULL);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 1000ull * BLK), (dmdrvi_ssize_t)BLK);
    DMOD_TEST_EXPECT_EQ(stats(false).last_address, 1000ull * BLK);
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf, 1000ull * BLK, BLK));
}

DMOD_TEST_STEP(dmsdio_end_of_device)
{
    const uint64_t end = 8ull * GIB;
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, end), 0);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 2 * BLK, end - 100), 100);
    DMOD_TEST_EXPECT_EQ(card_write(g_buf, BLK, end), -ENOSPC);
    DMOD_TEST_EXPECT_EQ(card_write(g_buf, 2 * BLK, end - BLK), (dmdrvi_ssize_t)BLK);
}

DMOD_TEST_STEP(dmsdio_argument_validation)
{
    dmdrvi_dev_num_t num = card_num();
    void* ro = g_drv.open(g_ctx, DMDRVI_O_RDONLY, &num);
    DMOD_TEST_EXPECT_EQ(g_drv.read(g_ctx, g_card, g_buf, BLK, -1), -EINVAL);
    DMOD_TEST_EXPECT_EQ(g_drv.read(g_ctx, g_card, g_buf, (size_t)INT64_MAX + 1u, 0), -EOVERFLOW);
    DMOD_TEST_EXPECT_EQ(g_drv.read(g_ctx, g_card, g_buf, 0, 0), 0);
    DMOD_TEST_EXPECT_EQ(g_drv.write(g_ctx, ro, g_buf, BLK, 0), -EBADF);
    DMOD_TEST_EXPECT_EQ(g_drv.read(g_ctx, g_host, g_buf, BLK, 0), -ENOTSUP);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_MONITOR_REFRESH, NULL), -ENOTTY);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_host, DMDRVI_IOCTL_BLOCK_GET_INFO, g_buf), -ENOTTY);
    g_drv.close(g_ctx, ro);
}

/* ======================================================================
 *  Erase / write protection
 * ====================================================================== */

DMOD_TEST_STEP(dmsdio_erase_and_discard)
{
    dmdrvi_block_range_t range = { 20 * BLK, 2 * BLK };
    dmdrvi_block_range_t bad   = { 20 * BLK + 1, BLK };
    fill(g_ref, 2 * BLK, 0x42);
    DMOD_TEST_EXPECT_EQ(card_write(g_ref, 2 * BLK, 20 * BLK), (dmdrvi_ssize_t)(2 * BLK));
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_BLOCK_ERASE, &range), 0);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 2 * BLK, 20 * BLK), (dmdrvi_ssize_t)(2 * BLK));
    memset(g_ref, 0, 2 * BLK);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_buf, g_ref, 2 * BLK));
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_BLOCK_ERASE, &bad), -EINVAL);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_BLOCK_DISCARD, &range), -ENOTSUP);

    use_card(dmsdio_card_type_sdxc, NULL);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_BLOCK_DISCARD, &range), 0);
}

DMOD_TEST_STEP(dmsdio_write_protected_card)
{
    dmsdio_port_mock_insert(HOST, dmsdio_card_type_sdhc);
    dmsdio_port_mock_set_write_protect(HOST, true);
    start_driver(NULL);
    dmdrvi_block_info_t block;
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_BLOCK_GET_INFO, &block), 0);
    DMOD_TEST_EXPECT_TRUE((block.flags & DMDRVI_BLOCK_FLAG_READ_ONLY) != 0);
    DMOD_TEST_EXPECT_EQ(card_write(g_buf, BLK, 0), -EROFS);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
    DMOD_TEST_EXPECT_EQ(stats(false).commands[24], 0u);
}

/* ======================================================================
 *  Errors, retries and recovery
 * ====================================================================== */

DMOD_TEST_STEP(dmsdio_transient_crc_is_recovered_by_retry)
{
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_data_crc, 17, 1);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
    DMOD_TEST_EXPECT_TRUE(matches_pattern(g_buf, 0, BLK));
    DMOD_TEST_EXPECT_EQ(host_info().retry_count, 1u);
    DMOD_TEST_EXPECT_EQ(host_info().last_error, -EBADMSG);
    DMOD_TEST_EXPECT_EQ(stats(false).faults_injected, 1u);
}

DMOD_TEST_STEP(dmsdio_multi_block_write_retry_restores_transfer_state)
{
    fill(g_ref, 4 * BLK, 0x21);
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_data_timeout, 25, 1);
    DMOD_TEST_EXPECT_EQ(card_write(g_ref, 4 * BLK, 0), (dmdrvi_ssize_t)(4 * BLK));
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 4 * BLK, 0), (dmdrvi_ssize_t)(4 * BLK));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_buf, g_ref, 4 * BLK));
    DMOD_TEST_EXPECT_TRUE(stats(false).aborts >= 1u);
}

DMOD_TEST_STEP(dmsdio_retry_exhaustion_returns_eio)
{
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_data_crc, 17, DMSDIO_MOCK_FOREVER);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), -EIO);
    DMOD_TEST_EXPECT_EQ(stats(false).faults_injected, 3u);     /* 1 + retries=2 */
    DMOD_TEST_EXPECT_TRUE(host_info().card_attached);
}

DMOD_TEST_STEP(dmsdio_precise_errors_without_retries)
{
    use_card(dmsdio_card_type_sdhc, "retries=0\n");
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_data_crc, 17, 1);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), -EBADMSG);
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_cmd_crc, 24, 1);
    DMOD_TEST_EXPECT_EQ(card_write(g_buf, BLK, 0), -EBADMSG);
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_cmd_timeout, 17, 1);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), -ETIMEDOUT);
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_data_timeout, 18, 1);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 2 * BLK, 0), -ETIMEDOUT);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
}

DMOD_TEST_STEP(dmsdio_malformed_response_is_not_retried)
{
    dmsdio_port_mock_inject_fault(HOST, dmsdio_mock_fault_bad_index, 17, 1);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), -EPROTO);
    DMOD_TEST_EXPECT_EQ(stats(false).faults_injected, 1u);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
}

DMOD_TEST_STEP(dmsdio_removal_during_transfer)
{
    void* old = g_card;
    dmsdio_port_mock_remove_after_blocks(HOST, 2);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 8 * BLK, 0), -ENODEV);
    DMOD_TEST_EXPECT_FALSE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(host_info().last_error, -ENODEV);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), -ENODEV);
    DMOD_TEST_EXPECT_EQ(g_drv.flush(g_ctx, old), -ENODEV);
    dmdrvi_dev_num_t num = card_num();
    DMOD_TEST_EXPECT_NULL(g_drv.open(g_ctx, DMDRVI_O_RDWR, &num));
    /* The node is withdrawn by the next REFRESH, not by the failing transfer. */
    DMOD_TEST_EXPECT_EQ(g_unavailable, 0);
    DMOD_TEST_EXPECT_EQ(refresh(), -ENODEV);
    DMOD_TEST_EXPECT_EQ(g_unavailable, 1);
    DMOD_TEST_EXPECT_EQ(refresh(), -ENODEV);
    DMOD_TEST_EXPECT_EQ(g_unavailable, 1);
}

DMOD_TEST_STEP(dmsdio_card_swapped_after_removal_during_transfer)
{
    /* Pulled mid-transfer, another card in before the monitor refreshes:
     * the old node is withdrawn before the new card is announced. */
    dmsdio_port_mock_remove_after_blocks(HOST, 1);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, 4 * BLK, 0), -ENODEV);
    dmsdio_port_mock_insert(HOST, dmsdio_card_type_sdxc);
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_EQ(g_unavailable, 1);
    DMOD_TEST_EXPECT_EQ(g_available, 2);
    DMOD_TEST_EXPECT_EQ(card_info().type, dmsdio_card_type_sdxc);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), -ESTALE);
}

DMOD_TEST_STEP(dmsdio_stale_handle_after_card_swap)
{
    uint32_t generation = host_info().generation;
    dmsdio_port_mock_remove(HOST);
    DMOD_TEST_EXPECT_EQ(refresh(), -ENODEV);
    dmsdio_port_mock_insert(HOST, dmsdio_card_type_sdxc);
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_EQ(g_unavailable, 1);
    DMOD_TEST_EXPECT_EQ(g_available, 2);
    DMOD_TEST_EXPECT_EQ(host_info().generation, generation + 2u);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), -ESTALE);
    dmsdio_card_info_t info;
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, dmsdio_ioctl_cmd_get_card_info, &info), -ESTALE);
    open_card();
    DMOD_TEST_EXPECT_NOT_NULL(g_card);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
}

DMOD_TEST_STEP(dmsdio_refresh_keeps_present_card)
{
    uint32_t generation = host_info().generation;
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_EQ(host_info().generation, generation);
    DMOD_TEST_EXPECT_EQ(g_available, 1);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
}

/* ======================================================================
 *  dmdrvi monitor contract (driven by dmdevmon)
 * ====================================================================== */

DMOD_TEST_STEP(dmsdio_create_leaves_the_card_to_the_monitor)
{
    /* No thread, no service, no bus traffic: the first REFRESH identifies. */
    stop_driver();
    g_available = 0;
    dmini_context_t ini = dmini_create();
    dmini_parse_string(ini, "[dmsdio]\ninstance=1\n");
    g_ctx = g_drv.create(ini, &g_host_num);
    dmini_destroy(ini);
    g_host = g_drv.open(g_ctx, DMDRVI_O_RDWR, &g_host_num);
    DMOD_TEST_EXPECT_FALSE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(host_info().scan_count, 0u);
    g_drv.path_ready(g_ctx, &g_host_num, "/dev/dmsdio0");
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_TRUE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(g_available, 1);
    Dmod_ThreadSleep(50);
    DMOD_TEST_EXPECT_EQ(host_info().scan_count, 1u);   /* nothing polls behind our back */
}

DMOD_TEST_STEP(dmsdio_monitor_policy_is_handed_out)
{
    use_card(dmsdio_card_type_sdhc, "monitor_event_handler=sd0_card_detect\n"
                                    "monitor_settle_ms=20\npoll_interval_ms=250\n");
    dmdrvi_monitor_policy_t policy;
    memset(&policy, 0xFF, sizeof(policy));
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_host, DMDRVI_IOCTL_MONITOR_GET_POLICY, &policy), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(policy.event_handler, "sd0_card_detect"), 0);
    DMOD_TEST_EXPECT_EQ(policy.settle_ms, 20u);
    DMOD_TEST_EXPECT_EQ(policy.poll_interval_ms, 250u);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_host, DMDRVI_IOCTL_MONITOR_GET_POLICY, NULL), -EINVAL);

    /* Defaults: no event handler, 50 ms settle time, polling every second. */
    stop_driver();
    dmini_context_t ini = dmini_create();
    dmini_parse_string(ini, "[dmsdio]\ninstance=1\n");
    g_ctx = g_drv.create(ini, &g_host_num);
    dmini_destroy(ini);
    g_host = g_drv.open(g_ctx, DMDRVI_O_RDWR, &g_host_num);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_host, DMDRVI_IOCTL_MONITOR_GET_POLICY, &policy), 0);
    DMOD_TEST_EXPECT_EQ(policy.event_handler[0], '\0');
    DMOD_TEST_EXPECT_EQ(policy.settle_ms, 50u);
    DMOD_TEST_EXPECT_EQ(policy.poll_interval_ms, 1000u);
}

DMOD_TEST_STEP(dmsdio_card_node_is_not_monitored)
{
    /* dmdevfs reports only the host node as a "monitor" device. */
    dmdrvi_monitor_policy_t policy;
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_MONITOR_GET_POLICY, &policy), -ENOTTY);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_MONITOR_EVENT, NULL), -ENOTTY);
    DMOD_TEST_EXPECT_EQ(g_drv.ioctl(g_ctx, g_card, DMDRVI_IOCTL_MONITOR_REFRESH, NULL), -ENOTTY);
}

DMOD_TEST_STEP(dmsdio_event_without_card_detect_pin)
{
    /* Nothing to sample - the event leaves everything to REFRESH. */
    DMOD_TEST_EXPECT_EQ(monitor_event(), 0);
    DMOD_TEST_EXPECT_EQ(card_read(g_buf, BLK, 0), (dmdrvi_ssize_t)BLK);
    DMOD_TEST_EXPECT_EQ(g_unavailable, 0);
}

DMOD_TEST_STEP(dmsdio_refresh_follows_removal_and_insertion)
{
    /* What dmdevmon does on every poll interval without a card detect pin. */
    dmsdio_port_mock_remove(HOST);
    DMOD_TEST_EXPECT_EQ(refresh(), -ENODEV);
    DMOD_TEST_EXPECT_FALSE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(g_unavailable, 1);
    DMOD_TEST_EXPECT_EQ(refresh(), -ENODEV);
    DMOD_TEST_EXPECT_EQ(g_unavailable, 1);
    dmsdio_port_mock_insert(HOST, dmsdio_card_type_sdxc);
    DMOD_TEST_EXPECT_EQ(refresh(), 0);
    DMOD_TEST_EXPECT_TRUE(host_info().card_attached);
    DMOD_TEST_EXPECT_EQ(g_available, 2);
    DMOD_TEST_EXPECT_EQ(card_info().type, dmsdio_card_type_sdxc);
}

/* ======================================================================
 *  Register decoders (public API)
 * ====================================================================== */

DMOD_TEST_STEP(dmsdio_decode_csd_versions)
{
    /* CSD v2, C_SIZE = 0x3FFF -> 8 GiB */
    uint32_t v2[4] = { 0x400E0032u, 0x5B590000u, 0x3FFF7F80u, 0x0A400001u };
    uint32_t v3[4] = { 0x800E0032u, 0x5B590000u, 0x3FFF7F80u, 0x0A400001u };
    dmsdio_csd_t csd;
    DMOD_TEST_EXPECT_EQ(dmsdio_decode_csd(v2, &csd), 0);
    DMOD_TEST_EXPECT_EQ(csd.structure, 1);
    DMOD_TEST_EXPECT_EQ(csd.capacity_bytes, 8ull * GIB);
    DMOD_TEST_EXPECT_EQ(csd.ccc, 0x5B5);
    DMOD_TEST_EXPECT_EQ(dmsdio_decode_csd(v3, &csd), -ENOTSUP);
    DMOD_TEST_EXPECT_EQ(dmsdio_decode_csd(NULL, &csd), -EINVAL);
}

DMOD_TEST_STEP(dmsdio_decode_scr_and_ssr)
{
    const uint8_t scr[8] = { 0x02, 0x35, 0x80, 0x02, 0, 0, 0, 0 };
    const uint8_t bad_scr[8] = { 0x12, 0x35, 0, 0, 0, 0, 0, 0 };
    uint8_t ssr_raw[64];
    dmsdio_scr_t s;
    dmsdio_ssr_t ssr;
    DMOD_TEST_EXPECT_EQ(dmsdio_decode_scr(scr, &s), 0);
    DMOD_TEST_EXPECT_EQ(s.sd_spec, 2);
    DMOD_TEST_EXPECT_TRUE(s.sd_spec3);
    DMOD_TEST_EXPECT_TRUE(s.bus_width_4bit);
    DMOD_TEST_EXPECT_TRUE(s.cmd23_supported);
    DMOD_TEST_EXPECT_EQ(dmsdio_decode_scr(bad_scr, &s), -EPROTO);

    memset(ssr_raw, 0, sizeof(ssr_raw));
    ssr_raw[0] = 0x80;
    ssr_raw[10] = 0xA0;         /* AU_SIZE 0xA = 8 MiB */
    ssr_raw[24] = 0x02;         /* DISCARD_SUPPORT */
    DMOD_TEST_EXPECT_EQ(dmsdio_decode_ssr(ssr_raw, &ssr), 0);
    DMOD_TEST_EXPECT_EQ(ssr.bus_width_code, 2);
    DMOD_TEST_EXPECT_EQ(ssr.au_size_bytes, 8u * 1024u * 1024u);
    DMOD_TEST_EXPECT_TRUE(ssr.discard_supported);
}
