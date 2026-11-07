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

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        Dmod_Printf("Usage: sdtest <host node> [MiB to read]   e.g. sdtest /dev/dmsdio0 4   (read-only)\n");
        return 1;
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
