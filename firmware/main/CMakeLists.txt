# main/ -- spinel ESP32-C3 firmware component (M2)
#
# One TU: the spinel-generated app.c (emitted by tools/build-app.sh
# --target=esp32) plus the app_main adapter. The prebuilt RT archive
# (libspinel_rt_esp32.a) is an IMPORTED target (spinel_rt, declared at the
# top level via add_prebuilt_library), so it is linked here rather than
# listed in REQUIRES. The shim headers shadow the POSIX surface newlib +
# FreeRTOS do not provide.

idf_component_register(
    SRCS "sp_app_adapter.c" "sp_lwip_compat.c" "sp_ota_adapter.c" "app.c"
    INCLUDE_DIRS "." "../../shim/esp32/include" "../../vendor/spinel/lib" "../../vendor/spinel/lib/regexp"
    # M4: esp_wifi STA + nvs + netif + event loop + adc driver. The
    # prebuilt RT archive cannot carry these; the app TU (FFI adapters)
    # and the IDF link line resolve them here. (sp_lwip_compat.c is
    # self-contained -- it needs no lwip headers -- but lwip stays a
    # dependency: it provides the lwip_* symbols the compat TU calls.)
    # M5a: mdns is the managed component espressif/mdns (idf_component.yml)
    # -- not in the IDF v5.5 component tree; the component manager fetches
    # it at configure time into build/managed_components.
    REQUIRES esp_wifi nvs_flash esp_netif esp_event esp_adc esp_driver_gpio lwip mdns app_update
)

# Link the prebuilt spinel RT after this component's objects. Imported
# targets from add_prebuilt_library land as -l<name>, which the IDF link
# line does not resolve against the archive path -- pass the full path.
target_link_libraries(${COMPONENT_LIB} PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../build/rt-riscv32/libspinel_rt_esp32.a)
# The RT archive sits LAST on the IDF link line, after newlib/libc, so the
# archive scan never loops back for symbols its objects reference (ld
# scans left-to-right once). Two groups of late-reference victims:
#   1. poll() lives only in libnewlib.a (newlib/src/poll.c, VFS-aware via
#      select()); without an anchor the RT's poll references (sp_io.c/
#      sp_sched.c/sp_cold.c) go unsatisfied. -u makes ld treat poll as
#      undefined from the start, pulling poll.o from newlib's archive
#      member regardless of scan order.
#   2. sp_lwip_compat.c's POSIX wrappers (socket/bind/.../freeaddrinfo,
#      lstat) live in libmain.a at position ~167 -- EARLY on the link
#      line. The RT archive (~1267, LAST) references them, but when ld
#      scanned libmain.a nothing needed them yet, so the member was
#      skipped. Anchoring each name forces sp_lwip_compat.o extraction
#      when libmain.a passes by. The wrappers in turn reference lwip_*
#      symbols; lwip.a (position ~79) precedes libmain.a, so those need
#      anchors too (they force sockets.c.obj/netdb.c.obj out of
#      liblwip.a). Every anchor is satisfied by the first archive that
#      defines the name; later archives are skipped, so the anchor adds
#      no duplicate-symbol risk.
foreach(_sym
    poll socket bind listen accept connect shutdown setsockopt
    getsockopt getsockname getpeername send sendto recv recvfrom
    getaddrinfo freeaddrinfo lstat
    lwip_socket lwip_bind lwip_listen lwip_accept lwip_connect
    lwip_shutdown lwip_setsockopt lwip_getsockopt lwip_send
    lwip_sendto lwip_recv lwip_recvfrom lwip_getaddrinfo
    lwip_freeaddrinfo)
    target_link_options(${COMPONENT_LIB} PUBLIC "-Wl,-u,${_sym}")
endforeach()
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
    SP_DYN_SYMS_MAX=256
    SP_EXC_STACK_MAX=8
    SP_CATCH_STACK_MAX=8
    SP_NO_SIGNALS
    __ESP32_SHIM__
    SP_INT_OVERFLOW_MODE_RAISE)