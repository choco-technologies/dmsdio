#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include "dmsdio_types.h"
#include <string.h>

#define SECTOR      512U
#define CHUNK       (16U * SECTOR)

static const char *card_type_name(dmsdio_card_type_t type)
{
    switch (type)
    {
        case dmsdio_card_type_sdsc_v1: return "SDSC v1";
        case dmsdio_card_type_sdsc_v2: return "SDSC v2";
        case dmsdio_card_type_sdhc:    return "SDHC";
        case dmsdio_card_type_sdxc:    return "SDXC";
        default:                       return "none";
    }
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void show_sector0(const uint8_t *s)
{
    Dmod_Printf("sector 0 starts:");
    for (int i = 0; i < 32; i++) Dmod_Printf(" %02x", s[i]);
    Dmod_Printf("\n");
    Dmod_Printf("sector 0: signature %02x%02x, OEM '%.8s'\n", s[511], s[510], (const char *)&s[3]);
    if (s[510] != 0x55 || s[511] != 0xAA)
    {
        Dmod_Printf("  no 0x55AA signature (not a partition table or a FAT boot sector)\n");
        return;
    }
    if (memcmp(&s[3], "EXFAT   ", 8) == 0)
        Dmod_Printf("  an exFAT volume without a partition table\n");
    else if (memcmp(&s[82], "FAT32   ", 8) == 0)
        Dmod_Printf("  a FAT32 volume without a partition table\n");
    for (unsigned i = 0; i < 4; i++)
    {
        const uint8_t *e = &s[446 + 16 * i];
        if (e[4] == 0)
            continue;
        Dmod_Printf("  partition %u: type 0x%02x, start sector %u, %u sectors\n", i + 1, e[4], (unsigned)le32(&e[8]), (unsigned)le32(&e[12]));
    }
}

static unsigned now_ms(void)
{
    return (unsigned)Dmod_GetUptime();
}

/* A test of the writes through a file system (the card mounted): one NEW file with a known pattern, written, closed, opened again and
 * compared. It never opens an existing file for writing (the open fails when the file is there) and removes nothing. */
static uint32_t pattern_word(uint32_t index)
{
    uint32_t x = index * 2654435761U + 0x9E3779B9U;
    x ^= x >> 15;
    x *= 2246822519U;
    x ^= x >> 13;
    return x;
}

static int fs_test(const char *path, unsigned kib)
{
    static uint8_t buffer[8192];
    uint32_t total = kib << 10;
    uint32_t done;

    void *probe = Dmod_FileOpen(path, "rb");
    if (probe != NULL)
    {
        Dmod_FileClose(probe);
        Dmod_Printf("sdtest: '%s' exists - not touching it, pick another name\n", path);
        return 1;
    }
    void *file = Dmod_FileOpen(path, "wb");
    if (file == NULL)
    {
        Dmod_Printf("sdtest: cannot create '%s'\n", path);
        return 1;
    }
    unsigned start = now_ms();
    for (done = 0; done < total; done += sizeof(buffer))
    {
        uint32_t chunk = total - done < sizeof(buffer) ? total - done : (uint32_t)sizeof(buffer);
        for (uint32_t i = 0; i < chunk; i += 4)
        {
            uint32_t w = pattern_word((done + i) / 4U);
            buffer[i] = (uint8_t)w; buffer[i + 1] = (uint8_t)(w >> 8); buffer[i + 2] = (uint8_t)(w >> 16); buffer[i + 3] = (uint8_t)(w >> 24);
        }
        if (Dmod_FileWrite(buffer, 1, chunk, file) != chunk)
        {
            Dmod_Printf("sdtest: write of %u bytes at %u failed\n", (unsigned)chunk, (unsigned)done);
            Dmod_FileClose(file);
            return 1;
        }
    }
    Dmod_FileClose(file);
    unsigned write_ms = now_ms() - start;
    Dmod_Printf("wrote %u KiB to %s in %u ms: %u KiB/s\n", kib, path, write_ms, write_ms != 0 ? kib * 1000U / write_ms : 0U);

    file = Dmod_FileOpen(path, "rb");
    if (file == NULL)
    {
        Dmod_Printf("sdtest: cannot open '%s' again\n", path);
        return 1;
    }
    if (Dmod_FileSize(file) != total)
        Dmod_Printf("sdtest: the size is %u, not %u\n", (unsigned)Dmod_FileSize(file), (unsigned)total);
    unsigned bad = 0;
    start = now_ms();
    for (done = 0; done < total; done += sizeof(buffer))
    {
        uint32_t chunk = total - done < sizeof(buffer) ? total - done : (uint32_t)sizeof(buffer);
        if (Dmod_FileRead(buffer, 1, chunk, file) != chunk)
        {
            Dmod_Printf("sdtest: read of %u bytes at %u failed\n", (unsigned)chunk, (unsigned)done);
            bad++;
            break;
        }
        for (uint32_t i = 0; i < chunk; i += 4)
            if (le32(&buffer[i]) != pattern_word((done + i) / 4U) && bad++ < 4)
                Dmod_Printf("sdtest: mismatch at %u\n", (unsigned)(done + i));
    }
    Dmod_FileClose(file);
    unsigned read_ms = now_ms() - start;
    Dmod_Printf("read back in %u ms: %u KiB/s, %u mismatches\n", read_ms, read_ms != 0 ? kib * 1000U / read_ms : 0U, bad);
    return bad != 0;
}


/* Raw writes to the card (it is overwritten: only for a card with nothing on it): one sector (CMD24), 16 sectors (CMD25), a partial sector
 * (read-modify-write) and 256 KiB, each read back; at `mib` MiB from the start of the card. */
static int fill(uint8_t *b, uint32_t n, uint32_t seed)
{
    for (uint32_t i = 0; i < n; i += 4)
    {
        uint32_t w = pattern_word(seed + i / 4U);
        b[i] = (uint8_t)w; b[i + 1] = (uint8_t)(w >> 8); b[i + 2] = (uint8_t)(w >> 16); b[i + 3] = (uint8_t)(w >> 24);
    }
    return 0;
}

static int raw_step(void *card, const char *name, uint64_t offset, uint32_t size, uint32_t seed)
{
    static uint8_t out[8192], in[8192];
    unsigned bad = 0;
    unsigned start = now_ms();
    for (uint32_t done = 0; done < size; done += sizeof(out))
    {
        uint32_t chunk = size - done < sizeof(out) ? size - done : (uint32_t)sizeof(out);
        fill(out, chunk, seed + done / 4U);
        Dmod_FileSeek(card, (Dmod_FileOffset_t)(offset + done), DMOD_SEEK_SET);
        if (Dmod_FileWrite(out, 1, chunk, card) != chunk)
        {
            Dmod_Printf("  %s: write of %u at %u failed\n", name, (unsigned)chunk, (unsigned)done);
            return 1;
        }
    }
    unsigned write_ms = now_ms() - start;
    start = now_ms();
    for (uint32_t done = 0; done < size; done += sizeof(out))
    {
        uint32_t chunk = size - done < sizeof(out) ? size - done : (uint32_t)sizeof(out);
        fill(out, chunk, seed + done / 4U);
        Dmod_FileSeek(card, (Dmod_FileOffset_t)(offset + done), DMOD_SEEK_SET);
        if (Dmod_FileRead(in, 1, chunk, card) != chunk)
        {
            Dmod_Printf("  %s: read of %u at %u failed\n", name, (unsigned)chunk, (unsigned)done);
            return 1;
        }
        if (memcmp(in, out, chunk) != 0)
            bad++;
    }
    unsigned read_ms = now_ms() - start;
    Dmod_Printf("  %s: %u bytes, write %u ms, read %u ms, %s\n", name, (unsigned)size, write_ms, read_ms, bad == 0 ? "OK" : "MISMATCH");
    return bad != 0;
}

static int raw_test(void *card, unsigned mib)
{
    uint64_t base = (uint64_t)mib << 20;
    static uint8_t keep[SECTOR], after[SECTOR], patch[100];
    int failed = 0;
    Dmod_Printf("raw write test at %u MiB\n", mib);
    failed += raw_step(card, "1 sector (CMD24)", base, SECTOR, 1U);
    failed += raw_step(card, "16 sectors (CMD25)", base + 0x10000, 16U * SECTOR, 2U);
    failed += raw_step(card, "256 KiB", base + 0x100000, 256U << 10, 3U);

    /* a partial sector: the bytes around it must stay */
    uint64_t at = base + 0x20000;
    fill(keep, SECTOR, 77U);
    Dmod_FileSeek(card, (Dmod_FileOffset_t)at, DMOD_SEEK_SET);
    Dmod_FileWrite(keep, 1, SECTOR, card);
    fill(patch, sizeof(patch), 99U);
    Dmod_FileSeek(card, (Dmod_FileOffset_t)(at + 300), DMOD_SEEK_SET);
    size_t w = Dmod_FileWrite(patch, 1, sizeof(patch), card);
    memcpy(keep + 300, patch, sizeof(patch));
    Dmod_FileSeek(card, (Dmod_FileOffset_t)at, DMOD_SEEK_SET);
    size_t r = Dmod_FileRead(after, 1, SECTOR, card);
    int ok = w == sizeof(patch) && r == SECTOR && memcmp(keep, after, SECTOR) == 0;
    Dmod_Printf("  partial sector (read-modify-write): %s\n", ok ? "OK" : "MISMATCH");
    failed += !ok;
    return failed;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        Dmod_Printf("Usage: sdtest <host node> [MiB to read]   e.g. sdtest /dev/dmsdio0 4   (read-only)\n"
                    "       sdtest --fs <new file> [KiB]       e.g. sdtest --fs /mnt/CARD/test.bin 1024   (writes ONE NEW file, removes nothing)\n");
        return 1;
    }
    if (strcmp(argv[1], "--fs") == 0)
    {
        unsigned kib = 256;
        if (argc < 3)
            return 1;
        if (argc > 3)
        {
            kib = 0;
            for (const char *p = argv[3]; *p >= '0' && *p <= '9'; p++)
                kib = kib * 10 + (unsigned)(*p - '0');
        }
        int r = fs_test(argv[2], kib);
        Dmod_Printf("sdtest: %s\n", r == 0 ? "done" : "FAILED");
        return r;
    }
    void *host = Dmod_FileOpen(argv[1], "r");
    if (host == NULL)
    {
        Dmod_Printf("sdtest: cannot open '%s'\n", argv[1]);
        return 1;
    }
    unsigned mib = 0;
    if (argc > 2)
        for (const char *p = argv[2]; *p >= '0' && *p <= '9'; p++)
            mib = mib * 10 + (unsigned)(*p - '0');

    unsigned raw_mib = 0;
    if (argc > 3 && strcmp(argv[2], "--raw") == 0)
    {
        mib = 0;
        for (const char *p = argv[3]; *p >= '0' && *p <= '9'; p++)
            raw_mib = raw_mib * 10 + (unsigned)(*p - '0');
        if (raw_mib == 0)
            raw_mib = 1;
    }
    int ret = Dmod_Ioctl(host, DMDRVI_IOCTL_MONITOR_REFRESH, NULL);
    Dmod_Printf("look for a card: %d%s\n", ret, ret == 0 ? "" : (ret == -19 ? " (no card)" : " (failed)"));
    dmsdio_host_info_t hi;
    if (Dmod_Ioctl(host, dmsdio_ioctl_cmd_get_host_info, &hi) == 0)
        Dmod_Printf("host %u: card %s, %u scans, last error %d, %u retries\n", hi.instance, hi.card_attached ? "attached" : "absent",
                    (unsigned)hi.scan_count, hi.last_error, (unsigned)hi.retry_count);
    dmsdio_card_info_t ci;
    int failed = 0;
    if (ret != 0 || Dmod_Ioctl(host, dmsdio_ioctl_cmd_get_card_info, &ci) != 0)
    {
        Dmod_FileClose(host);
        return 1;
    }
    Dmod_Printf("%s, %u MiB (%u sectors), %s addressing, %u-bit bus at %u Hz%s%s\n", card_type_name(ci.type),
                (unsigned)(ci.capacity_bytes >> 20), (unsigned)ci.block_count, ci.block_addressing ? "block" : "byte",
                (unsigned)ci.bus_width, (unsigned)ci.clock_hz, ci.high_speed ? ", high speed" : "", ci.write_protected ? ", write protected" : "");
    Dmod_Printf("CID: manufacturer 0x%02x, OEM '%.2s', product '%.5s' rev %u.%u, serial 0x%08x, %u/%u\n", ci.cid.manufacturer_id,
                ci.cid.oem_id, ci.cid.product_name, ci.cid.product_revision >> 4, ci.cid.product_revision & 0xF,
                (unsigned)ci.cid.serial_number, ci.cid.manufacture_month, ci.cid.manufacture_year);
    Dmod_Printf("SCR: spec %u%s, 4-bit %s; speed class %u\n", ci.scr.sd_spec, ci.scr.sd_spec3 ? " (3.x)" : "", ci.scr.bus_width_4bit ? "yes" : "no",
                ci.ssr.speed_class);

    char node[64];
    Dmod_SnPrintf(node, sizeof(node), "%s/0", argv[1]);
    /* The node of the card appears when dmdevfs has registered it, a moment after the host reported the card */
    void *card = NULL;
    for (unsigned attempt = 0; attempt < 50 && card == NULL; attempt++)
    {
        card = Dmod_FileOpen(node, "r");                /* reading only: a write on this handle is -EBADF */
        if (card == NULL)
        {
            unsigned wait = now_ms();
            while (now_ms() - wait < 100)
                ;
        }
    }
    if (card == NULL)
    {
        Dmod_Printf("sdtest: cannot open '%s'\n", node);
        Dmod_FileClose(host);
        return 1;
    }
    if (raw_mib != 0)
    {
        Dmod_FileClose(card);
        card = Dmod_FileOpen(node, "r+");
        if (card == NULL)
        {
            Dmod_Printf("sdtest: cannot open '%s' for writing\n", node);
            Dmod_FileClose(host);
            return 1;
        }
        int bad = raw_test(card, raw_mib);
        Dmod_Printf("sdtest: %s\n", bad == 0 ? "done" : "FAILED");
        Dmod_FileClose(card);
        Dmod_FileClose(host);
        return bad != 0;
    }
    static uint8_t buffer[CHUNK];
    if (Dmod_FileRead(buffer, 1, SECTOR, card) != SECTOR)
    {
        Dmod_Printf("sdtest: cannot read sector 0\n");
        failed++;
    }
    else
        show_sector0(buffer);

    if (mib != 0 && failed == 0)
    {
        uint32_t total = mib << 20, done = 0;
        uint32_t sum = 0;
        Dmod_FileSeek(card, 0, DMOD_SEEK_SET);
        unsigned start = now_ms();
        while (done < total)
        {
            size_t got = Dmod_FileRead(buffer, 1, CHUNK, card);
            if (got != CHUNK)
            {
                Dmod_Printf("sdtest: read of %u bytes at %u returned %u\n", CHUNK, (unsigned)done, (unsigned)got);
                failed++;
                break;
            }
            for (unsigned i = 0; i < CHUNK; i += 4)
                sum += le32(&buffer[i]);
            done += CHUNK;
        }
        unsigned ms = now_ms() - start;
        Dmod_Printf("read %u KiB in %u ms: %u KiB/s (checksum %08x)\n", (unsigned)(done >> 10), ms, ms != 0 ? (unsigned)((done >> 10) * 1000U / ms) : 0U,
                    (unsigned)sum);
        /* A few sectors from the end of the card: the address arithmetic of a large card */
        Dmod_FileSeek(card, (Dmod_FileOffset_t)((ci.block_count - 8U) * SECTOR), DMOD_SEEK_SET);
        if (Dmod_FileRead(buffer, 1, 8U * SECTOR, card) != 8U * SECTOR)
        {
            Dmod_Printf("sdtest: cannot read the last sectors\n");
            failed++;
        }
        else
            Dmod_Printf("the last 8 sectors read\n");
    }
    Dmod_Printf("sdtest: %s\n", failed == 0 ? "done" : "FAILED");
    Dmod_FileClose(card);
    Dmod_FileClose(host);
    return failed != 0;
}
