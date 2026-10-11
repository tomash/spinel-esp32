# main/ -- spinel ESP32-C3 firmware component (M2)
#
# One TU: the spinel-generated app.c (emitted by tools/build-app.sh
# --target=esp32) plus the app_main adapter. The prebuilt RT archive
# (libspinel_rt_esp32.a) is an IMPORTED target (spinel_rt, declared at the
# top level via add_prebuilt_library), so it is linked here rather than
# listed in REQUIRES. The shim headers shadow the POSIX surface newlib +
# FreeRTOS do not provide.

idf_component_register(
    SRCS "sp_app_adapter.c" "app.c"
    INCLUDE_DIRS "." "../../shim/esp32/include" "../../vendor/spinel/lib" "../../vendor/spinel/lib/regexp"
)

# Link the prebuilt spinel RT after this component's objects. Imported
# targets from add_prebuilt_library land as -l<name>, which the IDF link
# line does not resolve against the archive path -- pass the full path.
target_link_libraries(${COMPONENT_LIB} PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../build/rt-riscv32/libspinel_rt_esp32.a)
# spinel's header-heavy style trips unused-function warnings; IDF compiles
# with -Werror=all by default, and -Wno-error cannot undo -Werror=all once
# expanded. The RT archive was built -Wno-all for the same reason; the
# generated TU is vendor code, so silence warnings for it entirely (-w),
# keeping IDF's strictness for IDF's own components.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -w)

# Static-BSS capacity macros -- MUST match tools/build-rt.sh $SP_SZ exactly:
# both sides declare sp_gc_roots[SP_GC_STACK_MAX] (sp_gc.h:81) and the app
# TU's inline SP_GC_ROOT push/pop consults the same bound. A mismatch
# corrupts the root stack. (0040 patch guards the rest.)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SP_GC_STACK_MAX=4096
    SP_GC_REMEMBERED_MAX=1024
    SP_GC_PINNED_MAX=1024
    SP_ALLOC_STATS=512
    SP_STR_SHAPE_MAX=512
    SP_NO_SIGNALS
    __ESP32_SHIM__
    SP_INT_OVERFLOW_MODE_RAISE)