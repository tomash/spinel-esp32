# firmware/spinel_target.cmake -- shared target->archive mapping.
#
# Included by the top-level CMakeLists.txt and main/CMakeLists.txt (keep the
# two in sync by construction). The prebuilt spinel RT archive lives in
# build/rt-<arch>/ where arch follows tools/build-rt.sh's TARGET names:
#   esp32c3 -> rt-riscv32     (rv32imc)
#   esp32c6 -> rt-riscv32imac (rv32imac)
#   esp32   -> rt-esp32       (xtensa LX6, classic ESP32 / T-Display / M5StickC)
#   esp32s3 -> rt-esp32s3     (xtensa LX7, M5StickS3)
# IDF_TARGET is exported by idf.py as an environment variable at configure
# time; when unset (bare `cmake`), fall back to the sdkconfig's
# CONFIG_IDF_TARGET line if it exists, else default to esp32c3.

if(DEFINED ENV{IDF_TARGET} AND NOT "$ENV{IDF_TARGET}" STREQUAL "")
    set(SPINEL_IDF_TARGET "$ENV{IDF_TARGET}")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig" _spineltgt
         REGEX "^CONFIG_IDF_TARGET=\"")
    string(REGEX REPLACE "CONFIG_IDF_TARGET=\"([^\"]+)\"" "\\1" _spineltgt "${_spineltgt}")
    set(SPINEL_IDF_TARGET "${_spineltgt}")
else()
    set(SPINEL_IDF_TARGET "esp32c3")
endif()

# Variant overrides, read from the environment at configure time (so give
# each variant its own build dir, as tools/size-report.sh does):
#   SPINEL_SRC        spinel tree whose lib/ headers the app TU compiles
#                     against; must be the tree the RT archive was built from
#   SPINEL_RT_SUFFIX  archive dir build/rt-<arch><suffix> (tools/build-rt.sh
#                     RT_SUFFIX)
#   SPINEL_APP_DEFS   extra compile definitions for the app TU, space
#                     separated, without -D; must match tools/build-rt.sh
#                     RT_DEFS (e.g. "SP_NO_ALLOC_REPORT")
if(DEFINED ENV{SPINEL_SRC} AND NOT "$ENV{SPINEL_SRC}" STREQUAL "")
    set(SPINEL_SRC_DIR "$ENV{SPINEL_SRC}")
else()
    set(SPINEL_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../vendor/spinel")
endif()
separate_arguments(SPINEL_APP_DEFS UNIX_COMMAND "$ENV{SPINEL_APP_DEFS}")

# Capacity preset (presets/<name>.defs, shared with tools/build-rt.sh):
#   SPINEL_PRESET     default | small | ...; a non-default preset links the
#                     archive tools/build-rt.sh builds for it,
#                     build/rt-<arch>-<preset>, unless SPINEL_RT_SUFFIX is set
if(DEFINED ENV{SPINEL_PRESET} AND NOT "$ENV{SPINEL_PRESET}" STREQUAL "")
    set(SPINEL_PRESET "$ENV{SPINEL_PRESET}")
else()
    set(SPINEL_PRESET "default")
endif()
set(_spinel_preset_file "${CMAKE_CURRENT_LIST_DIR}/../presets/${SPINEL_PRESET}.defs")
if(NOT EXISTS "${_spinel_preset_file}")
    message(FATAL_ERROR "spinel: no preset ${_spinel_preset_file}")
endif()
file(STRINGS "${_spinel_preset_file}" SPINEL_PRESET_DEFS REGEX "^[ \t]*[A-Za-z_]")
list(TRANSFORM SPINEL_PRESET_DEFS STRIP)
if(DEFINED ENV{SPINEL_RT_SUFFIX})
    set(SPINEL_RT_SUFFIX "$ENV{SPINEL_RT_SUFFIX}")
elseif(NOT SPINEL_PRESET STREQUAL "default")
    set(SPINEL_RT_SUFFIX "-${SPINEL_PRESET}")
else()
    set(SPINEL_RT_SUFFIX "")
endif()

if(SPINEL_IDF_TARGET STREQUAL "esp32c3")
    set(SPINEL_RT_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/rt-riscv32${SPINEL_RT_SUFFIX}")
elseif(SPINEL_IDF_TARGET STREQUAL "esp32")
    set(SPINEL_RT_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/rt-esp32${SPINEL_RT_SUFFIX}")
elseif(SPINEL_IDF_TARGET STREQUAL "esp32c6")
    set(SPINEL_RT_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/rt-riscv32imac${SPINEL_RT_SUFFIX}")
elseif(SPINEL_IDF_TARGET STREQUAL "esp32s3")
    set(SPINEL_RT_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/rt-esp32s3${SPINEL_RT_SUFFIX}")
else()
    message(FATAL_ERROR
        "spinel: no prebuilt RT archive mapping for IDF_TARGET=${SPINEL_IDF_TARGET} "
        "(supported: esp32c3 -> build/rt-riscv32, esp32 -> build/rt-esp32, esp32c6 -> build/rt-riscv32imac, esp32s3 -> build/rt-esp32s3)")
endif()


# Board (firmware/main/boards/<board>.c, the board.h API the apps call):
#   SPINEL_BOARD  t_oi_plus | t_display | m5stamp_c3 | esp32_c6_devkitc |
#                 m5stickc | m5sticks3; each target has a default, the board
#                 every app here was first written for.
if(DEFINED ENV{SPINEL_BOARD} AND NOT "$ENV{SPINEL_BOARD}" STREQUAL "")
    set(SPINEL_BOARD "$ENV{SPINEL_BOARD}")
elseif(SPINEL_IDF_TARGET STREQUAL "esp32")
    set(SPINEL_BOARD "t_display")
elseif(SPINEL_IDF_TARGET STREQUAL "esp32c6")
    set(SPINEL_BOARD "esp32_c6_devkitc")
elseif(SPINEL_IDF_TARGET STREQUAL "esp32s3")
    set(SPINEL_BOARD "m5sticks3")
else()
    set(SPINEL_BOARD "t_oi_plus")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/main/boards/${SPINEL_BOARD}.c")
    message(FATAL_ERROR "spinel: no board file main/boards/${SPINEL_BOARD}.c")
endif()
set(SPINEL_RT_ARCHIVE "${SPINEL_RT_DIR}/libspinel_rt_esp32.a")
if(NOT EXISTS "${SPINEL_RT_ARCHIVE}")
    message(FATAL_ERROR
        "spinel: ${SPINEL_RT_ARCHIVE} not found -- run "
        "'tools/build-rt.sh <arch>' first (esp32c3: riscv32, esp32c6: riscv32imac, esp32: esp32, esp32s3: esp32s3)")
endif()
message(STATUS "spinel: IDF_TARGET=${SPINEL_IDF_TARGET} RT archive: ${SPINEL_RT_ARCHIVE} src: ${SPINEL_SRC_DIR} preset: ${SPINEL_PRESET} board: ${SPINEL_BOARD} defs: ${SPINEL_APP_DEFS}")
