#!/usr/bin/env python3
"""Capture ESP32-C3 serial output after a clean reset.

DTR/RTS sequencing (learned the hard way): pyserial asserts DTR on open,
which pulls BOOT/IO9 low -- every EN/RTS "reset" then releases EN with
BOOT still low and the chip re-enters download mode (silent). So:
release DTR FIRST, then pulse RTS for a clean reset into the app.

Env overrides:
  SPINEL_ESP_PORT     serial port (default /dev/ttyUSB0)
  SPINEL_ESP_CAPTURE  capture window seconds (default 12)
"""
import os, serial, time, sys

PORT = os.environ.get("SPINEL_ESP_PORT", "/dev/ttyUSB0")
WINDOW = float(os.environ.get("SPINEL_ESP_CAPTURE", "12"))
QUIET_EXT = 3.0        # extend window while data keeps arriving

p = serial.Serial(PORT, 115200, timeout=0.1)
p.dtr = False          # BOOT high -- run the app
time.sleep(0.3)
p.rts = True           # EN low
time.sleep(0.1)
p.rts = False          # EN high: reset, run

t0 = time.time()
buf = b""
while True:
    chunk = p.read(4096)
    if chunk:
        buf += chunk
    if time.time() - t0 > WINDOW and not chunk:
        break
    if time.time() - t0 > WINDOW + QUIET_EXT:
        break
p.close()
sys.stdout.write(buf.decode("utf-8", "replace"))
