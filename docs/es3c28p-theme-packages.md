# ES3C28P theme packages

The ES3C28P firmware keeps UI themes separate from the core firmware, speech
models, emoji assets, and SD music. A bad or interrupted theme update falls
back to the theme compiled into the core.

## Choose the built-in fallback at build time

Run `idf.py menuconfig`, then select:

`Xiaozhi Assistant -> ES3C28P built-in fallback theme`

The options are **Studio Precision** and **Storybook Garden (Kids)**. The
fallback is used whenever the external `theme` partition is empty or invalid.

## Build Storybook Garden separately

```sh
idf.py es3c28p_storybook_theme
```

This creates `build/storybook-garden.theme.bin`. It is not included in the
normal app flash target.

For a board connected over USB, flash only the theme partition:

```sh
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  write_flash 0xF80000 build/storybook-garden.theme.bin
```

Restart the board after flashing. The partition table must first be upgraded
once to `partitions/v2/16m_es3c28p.csv` by flashing a full ES3C28P build.

## Install from a URL

Host the `.theme.bin` file at a direct HTTP(S) URL and call the user-only MCP
tool `self.theme.install` with a `url` string. The board validates the package
CRC and manifest before restarting. Use `self.theme.get_info` to inspect the
active theme.

## Manage the device from a browser

When the board is connected to Wi-Fi, double-tap the Wi-Fi icon in the status
bar to show its IP address and local web address. Open that address from a
computer or phone on the same network, for example `http://192.168.1.42/`.

The local page provides:

- Device, Wi-Fi, battery, and active-theme status
- Volume and screen-brightness controls
- Theme installation from a local `.theme.bin` file
- Theme installation from a direct HTTP(S) URL
- Wi-Fi reset and device restart actions

Theme packages are streamed directly to the external `theme` partition. They
are not kept in RAM or on the SD card. The board checks the manifest and CRC
before restarting. The page uses a per-boot, same-origin token for write
actions and is intended only for use on a trusted local network.

## Package format

The file starts with the 16-byte little-endian header
`<magic=XTHM, format=1, reserved, json_length, json_crc32>`, followed by a
UTF-8 JSON manifest. The partition is 512 KiB; the current manifest limit is
64 KiB, leaving room for future packaged icons or small illustrations.
