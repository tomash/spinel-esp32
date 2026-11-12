# firmware-pico/ -- spinel-mote on the Raspberry Pi Pico SDK

Target: **Raspberry Pi Pico 2 W** (RP2350), on its RISC-V (Hazard3) cores.
Same pipeline as `firmware/` (ESP-IDF): spinel emits C for the app, which links
against a prebuilt `libspinel_rt`, the board layer (`firmware/main/board.h`) and
the network layer (`firmware/main/net.h`). As on ESP-IDF, the Ruby program runs
in a FreeRTOS task, and lwIP's BSD sockets carry `TCPServer` and friends, so
`app/wifi_status.rb` is the same file on both.

## One-time setup

Everything lives in `~/pico` (override with the variables in `tools/pico-app.sh`):

```sh
mkdir -p ~/pico && cd ~/pico
git clone --depth 1 -b 2.3.1 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init --depth 1 lib/cyw43-driver lib/tinyusb lib/lwip lib/btstack lib/mbedtls
git clone --depth 1 https://github.com/raspberrypi/FreeRTOS-Kernel.git   # next to pico-sdk
T=https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0
mkdir riscv-gcc picotool-bin
curl -sSL $T/riscv-toolchain-16-x86_64-lin.tar.gz | tar xz -C riscv-gcc
curl -sSL $T/picotool-2.3.1-x86_64-lin.tar.gz     | tar xz -C picotool-bin
# USB access for picotool (plugdev):
sudo cp ~/spinel-esp32/tools/udev/60-spinel-mote-rp2.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger --attr-match=idVendor=2e8a
```

Then the runtime archive: `tools/build-rt.sh rp2350-riscv`.

## Build, flash, watch

```sh
tools/build-app.sh app/hello.rb --target=rp2350 --monitor
tools/board-flash.sh pico2w app/board_info.rb      # with a boards.local line
```

The first flash needs the board in BOOTSEL (hold the button while plugging
in). After that, picotool reboots a running spinel-mote app into BOOTSEL over
its USB serial interface, so no button press is needed.

## Layout

| File | Role |
|---|---|
| `CMakeLists.txt` | SDK project; `main/app.c` alone gets the shim and runtime headers |
| `main/sp_pico_adapter.c` | `main()`: stdio (USB CDC + UART0), FreeRTOS, the generated program (renamed `spinel_app_main`) in a 48 KB task; boot guard, CPU-exception report, `exit()` and `nanosleep` |
| `main/sp_pico_sockets.c` | POSIX socket names onto `lwip_*`; newlib syscalls routed by fd (stdio 0-2, `/dev/null` 15, sockets 16+); `fcntl`/`ioctl`/`poll` with the value translation the runtime needs |
| `main/net_pico.c` | `net.h`: CYW43 station, lwIP mDNS and SNTP, heap figures, GC pacing; OTA answers -1 |
| `config/` | `FreeRTOSConfig.h` (SMP, heap_3) and `lwipopts.h` (sockets on, newlib errno) |
| `boards/pico2w.c` | LED on the CYW43 chip, BOOTSEL as button 0, VSYS on ADC3 |
| `../shim/rp2350/` | what the Pico newlib lacks beside IDF's: POSIX clocks, `nanosleep`, dirent, stdio locks |

## When something goes wrong

- **Boot guard.** If a build dies before its USB serial port is up, the
  watchdog (8 s) reboots it into BOOTSEL, so `picotool load` reaches it
  without a button press.
- **CPU exceptions** (misaligned access, bus fault, illegal instruction) are
  recorded and the board enters BOOTSEL; the next image to boot prints
  `spinel: previous run stopped on a CPU exception: ... at pc 0x...` (look
  the pc up with `riscv32-pico-elf-addr2line -fe build/spinel_mote_app.elf`).
  The Hazard3 cores trap on misaligned loads and stores, which the ESP32-C3
  tolerated: C code that writes through an `int *` into a Ruby String's bytes
  must `memcpy` instead.
- **Uncaught Ruby exception**: `exit()` stops the Ruby task only, so the
  message stays readable and picotool can still reach the board.
- **Open issue: soft reboots.** A reboot straight back into the app
  (`picotool reboot`, `watchdog_reboot`) hangs that boot before the
  scheduler starts. A power-up, and the boot after `picotool load` from
  BOOTSEL, are fine. Until it is understood, prefer flashing through BOOTSEL
  or power-cycling the board's hub port.

Measured (2026-11-12, preset default): `hello` 296 KB flash, 70 KB static
RAM (48 KB of it the program stack); `blink_board` 420 KB (the CYW43 firmware
blob, needed for the LED, is most of it); `wifi_status` 1.04 MB flash, 116 KB
static RAM, ~293 KB heap free while serving (the ESP32-C3: ~89 KB). 200/200
sequential and 40/40 parallel requests, mDNS, SNTP.
