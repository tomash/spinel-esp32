# firmware/ -- ESP-IDF v5.5 project for the spinel ESP32-C3 app (M2)
#
# Pipeline: tools/build-app.sh <app>.rb --target=esp32 emits main/app.c
# (spinel infers 32-bit ints by probing the cross gcc via --cc=), then
# `idf.py set-target esp32c3 && idf.py build` links it against the
# prebuilt RT archive (build/rt-riscv32/libspinel_rt_esp32.a, rv32imc).
#
# The Ruby body runs on the IDF main task's stack: the shim mmap is a
# MAP_FAILED stub, so sp_main_stack_run's fallback ("run it where we are")
# is the live path -- hence the raised main-task stack in sdkconfig.defaults.

cmake_minimum_required(VERSION 3.16)
# M6: distinct app version per build, so the OTA version guard
# (esp32_ota_check_version, mirroring native_ota_example) can tell a new
# image from the running one and refuse a no-op reflash. Must be set
# BEFORE project.cmake / project(): IDF stamps the app descriptor at
# project() time, falling back to git describe (which does not change
# across a working-tree rebuild).
set(PROJECT_VER "0.7.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(spinel_esp32_app C CXX ASM)

# Prebuilt spinel RT (built by tools/build-rt.sh for rv32imc, SP_NO_SIGNALS):
# a plain imported static library; the app's link line adds it after main.
add_prebuilt_library(spinel_rt ${CMAKE_CURRENT_LIST_DIR}/../build/rt-riscv32/libspinel_rt_esp32.a)