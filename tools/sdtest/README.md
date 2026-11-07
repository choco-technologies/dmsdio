# sdtest

A **read-only** test of a card on a dmsdio host (`/dev/dmsdio0`): it never writes to the card and never erases anything - the card node is
opened for reading only.

```
sdtest /dev/dmsdio0              # identify the card, read the first sector and the partition table
sdtest /dev/dmsdio0 4            # and read 4 MiB from the start (and a few sectors from the end), with the speed
```

It asks the host node to look for a card (`DMDRVI_IOCTL_MONITOR_REFRESH`), prints what the card says (type, capacity, CID, CSD, the bus width and
the clock the core negotiated), then opens `/dev/dmsdio<N>/0` for reading, prints the first sector (the MBR signature and the partitions, or
the name of the file system of a volume without a partition table) and reads the requested number of MiB with a check of the speed.
