# LCDWiki ES3C28P

Support for the LCDWiki ES3C28P 2.8-inch ESP32-S3 display module.

## Hardware

- ESP32-S3, 16 MB flash, 8 MB OPI PSRAM
- 240x320 ILI9341V SPI display
- FT6336G capacitive touch controller
- ES8311 audio codec, onboard microphone, and speaker amplifier
- Battery voltage monitoring on GPIO9
- One WS2812 status LED
- BOOT button on GPIO0
- MicroSD slot on a dedicated 4-bit SDMMC bus

The pin assignments are based on the manufacturer's
[product documentation](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display).

## Build

```bash
python scripts/release.py es3c28p
```

## Web install (Cloudflare Worker)

Flash from Chrome or Edge over USB with [ESP Web Tools](https://esphome.github.io/esp-web-tools/).
The page lives in [`web/installer/`](../../../web/installer/) and is served by the
`es3c28p-install` Worker ([`wrangler.jsonc`](../../../wrangler.jsonc)).

ESP-IDF cannot build on Cloudflare. Add the Worker in the dashboard yourself;
do not wire up an automatic deploy.

### One-time Cloudflare setup (dashboard)

1. Cloudflare dashboard → **Workers & Pages** → **Create** → **Connect to Git**.
2. Project name: `es3c28p-install` (must match `name` in `wrangler.jsonc`).
3. Build command: leave empty.
4. Deploy command: `npx wrangler deploy`.
5. Click **Deploy**.

The installer UI is enough to flash a local `merged-binary.bin`. Hosted
firmware on the Worker is optional: after a local build, run
`python scripts/prepare_installer.py` and deploy again from a machine that has
those `.bin` files (they stay gitignored).

### Local preview

```bash
python scripts/release.py es3c28p
python scripts/prepare_installer.py
python3 -m http.server 8080 --directory web/installer
```

Open [http://localhost:8080/](http://localhost:8080/), connect the board over USB,
click **Connect and install**. Hold **BOOT** if the serial port does not appear.
First install should erase flash so the 16 MB table (including the theme slot
at `0xF80000`) is written.

## Music from MicroSD

Insert a FAT32-formatted MicroSD card before boot. The player scans the card root
and subdirectories (up to four levels deep) for as many as 256 `.mp3` files.
Tracks are sorted by their full path.

Use the fixed `AI Chat` and `Music` tabs to switch between the two independent
screens. AI status and messages remain on the chat screen; playback state and the
current filename remain on the music screen.
The music screen also shows a progress bar and elapsed/total track time.
Use `Browse` to open folders on the SD card and select the MP3 file to play.
Hidden and temporary entries whose names start with `.` or `_` are ignored.

The top status bar shows the current speaker volume as a percentage.

On the Music screen, the touchscreen controls playback by horizontal zone:

- Left third: previous track
- Center third: play or pause
- Right third: next track

The display shows the playback state and current filename. Music pauses while the
assistant is listening or speaking, then continues when the device returns to
idle. Playback controls are locked while an AI conversation is active. The BOOT
button remains available for the normal assistant interaction.
