# dmsdio Configuration Files

Board configurations loaded by dmdevfs to bring the SD card slot up. Each
file configures the SD bus pins through dmgpio (AF12, pull-ups on CMD and
D0-D3), the optional card-detect GPIO (`friend_role=card_detect`) and the
dmsdio device itself, all in one `friends_group`. The pin sources are cited
in each file - cross-check them against your own board revision before
flashing.

```
configs/
└── board/
    ├── stm32f407g-disc1/
    │   └── sdio.ini        # external microSD breakout on SDIO
    └── stm32f746g-disco/
        └── sdmmc1.ini      # on-board microSD slot on SDMMC1
```

See [../docs/configuration.md](../docs/configuration.md) for every dmsdio key.
