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
The shared installer lives in [`web/installer/`](../../../web/installer/) and is
served by the `xiaozhi-install` Worker ([`wrangler.jsonc`](../../../wrangler.jsonc)).
The installer currently lists only ES3C28P.

ESP-IDF cannot build on Cloudflare. GitHub Actions compiles firmware with
`espressif/idf:release-v5.5`, packs `web/installer`, and deploys the Worker.

### One-time setup

1. Cloudflare dashboard → **Workers & Pages** → **Create** a Worker named
   `xiaozhi-install` (must match `name` in `wrangler.jsonc`). Do not connect
   Git on Cloudflare; that build cannot run ESP-IDF.
2. GitHub repo → **Settings** → **Secrets and variables** → **Actions**:
   - `CLOUDFLARE_API_TOKEN` (Account / Workers Scripts: Edit)
   - `CLOUDFLARE_ACCOUNT_ID`
3. Push to `main`, or run **Build Boards** → **Run workflow**. The installer
   packs ES3C28P only.

The workflow uploads a `xiaozhi-install` artifact on every run. Hosted
`.bin` files stay gitignored and are attached only in CI.

### Local preview

```bash
python scripts/release.py es3c28p
python scripts/prepare_installer.py es3c28p
python3 -m http.server 8080 --directory web/installer
```

Open [http://localhost:8080/?board=es3c28p](http://localhost:8080/?board=es3c28p),
connect the board over USB, click **Connect and install**. Hold **BOOT** if the
serial port does not appear. First install should erase flash so the 16 MB table
(including the theme slot at `0xF80000`) is written.

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
