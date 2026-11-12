# firmware-pico/ -- spinel-mote on the Raspberry Pi Pico SDK

Target: **Raspberry Pi Pico 2 W** (RP2350), on its RISC-V (Hazard3) cores.
Same pipeline as `firmware/` (ESP-IDF): spinel emits C for the app, which links
against a prebuilt `libspinel_rt` and the board layer (`firmware/main/board.h`).

## One-time setup

Everything lives in `~/pico` (override with the variables in `tools/pico-app.sh`):

```sh
mkdir -p ~/pico && cd ~/pico
git clone --depth 1 -b 2.3.1 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init --depth 1 lib/cyw43-driver lib/tinyusb lib/lwip lib/btstack lib/mbedtls
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
| `main/sp_pico_adapter.c` | `main()`: stdio (USB CDC + UART0), `board_init`, then the generated program, renamed `spinel_app_main` |
| `main/sp_pico_stack.S` | runs the program on a 48 KB static stack (the SDK's core-0 stack is 4 KB in SCRATCH_Y) |
| `boards/pico2w.c` | LED on the CYW43 chip, BOOTSEL as button 0, VSYS on ADC3 |
| `../shim/rp2350/` | what the Pico newlib lacks beside IDF's: POSIX clocks, `nanosleep`, dirent, stdio locks |

Measured (2026-11-12, preset default): `hello` 296 KB flash, 70 KB static
RAM (48 KB of it the program stack); `blink_board` 420 KB (the CYW43 firmware
blob, needed for the LED, is most of it).
