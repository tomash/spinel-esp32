# firmware-pico/ -- spinel-mote firmware for the Raspberry Pi Pico SDK
# (RP2350: Pico 2 W; RISC-V Hazard3 cores first, see docs/plan-other-mcus.md).
#
# The counterpart of firmware/ (ESP-IDF): one executable from the
# spinel-generated main/app.c, a startup adapter, the board file and the
# prebuilt runtime archive (tools/build-rt.sh rp2350-riscv). Configure through
# tools/build-app.sh --target=rp2350, or by hand:
#
#   PICO_SDK_PATH=~/pico/pico-sdk PICO_TOOLCHAIN_PATH=~/pico/riscv-gcc \
#   cmake -S firmware-pico -B firmware-pico/build -G Ninja
#
# Environment, as firmware/spinel_target.cmake: SPINEL_SRC, SPINEL_PRESET,
# SPINEL_RT_SUFFIX, SPINEL_APP_DEFS, SPINEL_BOARD (default pico2w).
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Pico SDK board")
set(PICO_PLATFORM rp2350-riscv CACHE STRING "Pico SDK platform")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(spinel_mote_pico C CXX ASM)
pico_sdk_init()
# FreeRTOS (Raspberry Pi's kernel fork; FREERTOS_KERNEL_PATH, default: next
# to the SDK) -- lwIP's socket API needs an RTOS, see config/FreeRTOSConfig.h.
include($ENV{PICO_SDK_PATH}/../FreeRTOS-Kernel/portable/ThirdParty/GCC/RP2350_RISC-V/FreeRTOS_Kernel_import.cmake)

set(SPINEL_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
if(DEFINED ENV{SPINEL_SRC} AND NOT "$ENV{SPINEL_SRC}" STREQUAL "")
    set(SPINEL_SRC_DIR "$ENV{SPINEL_SRC}")
else()
    set(SPINEL_SRC_DIR "${SPINEL_ROOT}/vendor/spinel")
endif()
separate_arguments(SPINEL_APP_DEFS UNIX_COMMAND "$ENV{SPINEL_APP_DEFS}")

# Capacity preset: the same presets/<name>.defs tools/build-rt.sh built the
# archive with (both sides size sp_gc_roots[] and the exception stacks).
if(DEFINED ENV{SPINEL_PRESET} AND NOT "$ENV{SPINEL_PRESET}" STREQUAL "")
    set(SPINEL_PRESET "$ENV{SPINEL_PRESET}")
else()
    set(SPINEL_PRESET "default")
endif()
set(_preset_file "${SPINEL_ROOT}/presets/${SPINEL_PRESET}.defs")
if(NOT EXISTS "${_preset_file}")
    message(FATAL_ERROR "spinel: no preset ${_preset_file}")
endif()
file(STRINGS "${_preset_file}" SPINEL_PRESET_DEFS REGEX "^[ \t]*[A-Za-z_]")
list(TRANSFORM SPINEL_PRESET_DEFS STRIP)
if(DEFINED ENV{SPINEL_RT_SUFFIX})
    set(SPINEL_RT_SUFFIX "$ENV{SPINEL_RT_SUFFIX}")
elseif(NOT SPINEL_PRESET STREQUAL "default")
    set(SPINEL_RT_SUFFIX "-${SPINEL_PRESET}")
else()
    set(SPINEL_RT_SUFFIX "")
endif()
set(SPINEL_RT_ARCHIVE "${SPINEL_ROOT}/build/rt-${PICO_PLATFORM}${SPINEL_RT_SUFFIX}/libspinel_rt_esp32.a")
if(NOT EXISTS "${SPINEL_RT_ARCHIVE}")
    message(FATAL_ERROR "spinel: ${SPINEL_RT_ARCHIVE} not found -- run 'tools/build-rt.sh ${PICO_PLATFORM}' first")
endif()

if(DEFINED ENV{SPINEL_BOARD} AND NOT "$ENV{SPINEL_BOARD}" STREQUAL "")
    set(SPINEL_BOARD "$ENV{SPINEL_BOARD}")
else()
    set(SPINEL_BOARD "pico2w")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${SPINEL_BOARD}.c")
    message(FATAL_ERROR "spinel: no board file firmware-pico/boards/${SPINEL_BOARD}.c")
endif()
message(STATUS "spinel: ${PICO_PLATFORM} RT archive: ${SPINEL_RT_ARCHIVE} preset: ${SPINEL_PRESET} board: ${SPINEL_BOARD}")

add_executable(spinel_mote_app
    main/sp_pico_adapter.c
    main/sp_pico_sockets.c
    main/net_pico.c
    boards/${SPINEL_BOARD}.c
    main/app.c)

# board.h and net.h (and wifi_secrets.h) are shared with the ESP firmware.
target_include_directories(spinel_mote_app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/config
    ${CMAKE_CURRENT_LIST_DIR}/main
    ${SPINEL_ROOT}/firmware/main
    ${CMAKE_CURRENT_LIST_DIR}/boards)

# The generated TU alone sees the shim and the runtime headers: the SDK's own
# sources compile into this executable too, and must keep newlib's headers.
# Its main() is renamed so the adapter's main() can set up stdio, the board
# and a stack big enough for the Ruby program first.
set(_app_flags
    -I${SPINEL_ROOT}/shim/rp2350/include
    -I${SPINEL_ROOT}/shim/esp32/include
    -I${SPINEL_SRC_DIR}/lib
    -I${SPINEL_SRC_DIR}/lib/regexp
    -DSP_SHIM_RP2350
    -Dmain=spinel_app_main
    -w)
foreach(_d ${SPINEL_PRESET_DEFS} SP_NO_SIGNALS __ESP32_SHIM__ SP_INT_OVERFLOW_MODE_RAISE ${SPINEL_APP_DEFS})
    list(APPEND _app_flags -D${_d})
endforeach()
set_source_files_properties(main/app.c PROPERTIES COMPILE_OPTIONS "${_app_flags}")

# Wait for the USB serial port to be opened (up to 5 s) so the program's
# first lines are not lost; UART0 on GP0/GP1 (the Debug Probe's UART) too.
target_compile_definitions(spinel_mote_app PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=5000)
pico_enable_stdio_usb(spinel_mote_app 1)
pico_enable_stdio_uart(spinel_mote_app 1)

target_link_libraries(spinel_mote_app
    pico_stdlib
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_mdns
    pico_lwip_sntp
    FreeRTOS-Kernel-Heap3
    hardware_adc
    ${SPINEL_RT_ARCHIVE})
target_link_options(spinel_mote_app PRIVATE -Wl,--gc-sections)

pico_add_extra_outputs(spinel_mote_app)
