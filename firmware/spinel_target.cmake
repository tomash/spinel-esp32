# firmware/spinel_target.cmake -- shared target->archive mapping.
#
# Included by the top-level CMakeLists.txt and main/CMakeLists.txt (keep the
# two in sync by construction). The prebuilt spinel RT archive lives in
# build/rt-<arch>/ where arch follows tools/build-rt.sh's TARGET names:
#   esp32c3 -> rt-riscv32   (rv32imc)
#   esp32   -> rt-esp32     (xtensa, classic ESP32 / LilyGO T-Display)
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

if(SPINEL_IDF_TARGET STREQUAL "esp32c3")
    set(SPINEL_RT_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/rt-riscv32")
elseif(SPINEL_IDF_TARGET STREQUAL "esp32")
    set(SPINEL_RT_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/rt-esp32")
else()
    message(FATAL_ERROR
        "spinel: no prebuilt RT archive mapping for IDF_TARGET=${SPINEL_IDF_TARGET} "
        "(supported: esp32c3 -> build/rt-riscv32, esp32 -> build/rt-esp32)")
endif()

set(SPINEL_RT_ARCHIVE "${SPINEL_RT_DIR}/libspinel_rt_esp32.a")
if(NOT EXISTS "${SPINEL_RT_ARCHIVE}")
    message(FATAL_ERROR
        "spinel: ${SPINEL_RT_ARCHIVE} not found -- run "
        "'tools/build-rt.sh ${SPINEL_IDF_TARGET}' first (esp32c3 takes no arg)")
endif()
message(STATUS "spinel: IDF_TARGET=${SPINEL_IDF_TARGET} RT archive: ${SPINEL_RT_ARCHIVE}")
