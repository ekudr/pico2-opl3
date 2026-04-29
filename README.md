# OPL3 FM Synthesizer on Raspberry Pi Pico 2

A real-time [OPL3 (YMF262)](https://en.wikipedia.org/wiki/Yamaha_OPL#OPL3) FM synthesizer emulator running on the Raspberry Pi Pico 2. It receives `.vgm` / `.vgz` music files from a PC over USB and plays them back through a passive RC low-pass filter on GPIO 16.

The synthesis engine is [dbopl](https://www.dosbox.com) (DOSBox OPL3 emulator). ISR budget is ~10 ms; actual render time is ~172 µs.

---

## Hardware

### Components

| Qty | Part |
|-----|------|
| 1 | Raspberry Pi Pico 2 (RP2350) |
| 1 | Resistor 1 kΩ |
| 1 | Capacitor 10 nF |
| 1 | 3.5 mm audio jack (or speaker with amplifier) |

### Circuit

```
Pico 2                                         Audio out
─────────────────────────────────────────────────────────
GPIO 16 ──┬── R1 (1 kΩ) ──┬── TIP (left + right)
           │               │
          GND             C1 (10 nF)
                           │
                          GND (sleeve)
```

PWM carrier: **49 717 Hz** — R1 + C1 form a first-order low-pass filter
with cutoff ≈ 15.9 kHz, attenuating the carrier by ~10 dB while passing
the full audio band.

### Wiring diagram

```
┌─────────────────────────────────────────┐
│           Raspberry Pi Pico 2           │
│                                         │
│  GPIO 16 (PWM) ──────────────── [1kΩ] ──┼──── TIP  ───┐
│                                         │             │  3.5 mm
│  GND ───────────────────────────────────┼──── SLEEVE ─┤  audio
│                                         │             │  jack
│  GPIO 23 (SMPS ctrl) — internal only   │    [10nF]  │
│                                         │      │     │
└─────────────────────────────────────────┘     GND    │
                                                 └──────┘
```

> **GPIO 23** is driven high in firmware to put the RP2350's onboard SMPS
> into fixed-frequency mode, which reduces switching noise in the audio output.
> No external wiring needed.

---

## Firmware

### Build

Requires [Pico SDK 2.2.0](https://github.com/raspberrypi/pico-sdk) and `arm-none-eabi-gcc`.

```sh
cmake -S . -B build -G Ninja
ninja -C build
```

Output: `build/opl3.uf2`

### Flash

**USB (BOOTSEL):** hold BOOTSEL, plug in USB, copy `build/opl3.uf2` to the
mass-storage drive that appears.

**SWD / OpenOCD:**
```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
        -c "program build/opl3.elf verify reset exit"
```

**picotool:**
```sh
picotool load -fx build/opl3.uf2
```

---

## Usage

### Requirements

```sh
pip install pyserial
```

### Play a file

```sh
python send_vgm.py /dev/ttyACM0 song.vgm
# or
python send_vgm.py /dev/ttyACM0 song.vgz
```

On Windows use `COMx` instead of `/dev/ttyACMx`.

The script:
1. Decompresses `.vgz` if needed
2. Sends the file to the Pico over USB CDC
3. Prints `Playback complete.` when the track finishes

### VGM files

VGM files for OPL3 / OPL2 can be found on [VGMRips](https://vgmrips.net) —
search for chip **YMF262** (OPL3) or **YM3812** (OPL2).

Supported VGM commands: `0x5E` / `0x5F` (OPL3 ports 0/1), `0x5A` / `0x5B`
(OPL2 / dual-OPL2), `0x61`–`0x63`, `0x70`–`0x7F` (waits), `0x66` (end).

---

## Architecture

```
Core 0                          Core 1
──────────────────────          ──────────────────────────────────
USB CDC receive                 OPL3 table init (once, ~1 s)
VGM parse + timing              DMA ISR @ 49 717 Hz
push → SPSC queue               pull queue → OPL3_WriteReg
                                OPL3_GenerateStream → PWM buffer
```

- **Audio pipeline:** DMA double-buffer (2 × 512 samples) → PWM → RC filter
- **Synthesis engine:** dbopl (integer-only after one-time float table init)
- **Timing:** VGM 44 100 Hz ticks converted to OPL3 49 716 Hz via ratio 1381/1225
- **Binary type:** `copy_to_ram` — all code copied to SRAM at boot, eliminating XIP cache misses
