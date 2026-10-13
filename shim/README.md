# shim/esp32/ -- FreeRTOS/newlib compatibility layer for libspinel_rt

Mirrors vendor/spinel/lib/wasi/ (the in-tree precedent for porting the runtime
to a constrained target): header shims + one .c of POSIX-surface stubs, so that
every runtime object *compiles*, and only a program that actually reaches for
an unsupported feature pays at link/run time (gc-sections drops the rest).

## Why per-header (not one big stub file)

The runtime `#include`s its POSIX surface from many TUs. newlib (ESP-IDF)
provides: string/stdlib/stdio/stdint/unistd/errno/math/limits/time/stdarg/
fcntl/ctype/sys/types.h + more. Missing or partial: signal.h, poll.h,
sys/mman.h, sys/socket.h, spawn.h, sys/resource.h, sys/wait.h, pthread.h
(single-threaded rt only needs no-op locking, already compiled out), and
ucontext.h (only under the non-ASM fiber path -- see src/ucontext.S).

## Status: DONE (M1)

`tools/build-rt.sh` compiles the full 32-member runtime + 5 regexp objects
against this shim and archives them into `build/rt-riscv32/libspinel_rt_esp32.a`
(3.87 MB, 39 members). Zero errors, cross-gcc 14.2.0, `-O2 -ffunction-sections
-fdata-sections`. Compile took 6 fix rounds; see the per-header notes below.

## Inventory (14 headers, 2 sources)

### include/ headers

| Header | Role | Key contents / fix history |
|---|---|---|
| `signal.h` | **full replacement** (guard `_SIGNAL_H_`) | struct sigaction with sa_sigaction union (SA_SIGINFO path), SA_SIGINFO/SA_ONSTACK/SA_RESTART, stack_t, sigaltstack decl, full `psig` set. newlib's own signal.h is shadowed and never parsed. |
| `sys/signal.h` | shadow | delegates to `signal.h`; keeps `#include <sys/signal.h>` sites (newlib also installs one) working with identical semantics. |
| `sys/mman.h` | replacement | PROT_READ/WRITE, MAP_PRIVATE/ANONYMOUS/NORESERVE, MAP_FAILED, mmap/munmap/madvise decls, MADV_NORMAL/RANDOM/SEQUENTIAL/WILLNEED/DONTNEED/DONTDUMP/DODUMP (Linux values). madvise exists only so the slab's release sites compile -- mmap always answers MAP_FAILED (ENOMEM), so every madvise call is dead code. |
| `sys/socket.h` | replacement | AF_UNSPEC/UNIX(1)/INET(2)/INET6(10), PF_* = AF_*, SOL_SOCKET 0xfff, SO_REUSEADDR 0x0004, SO_BROADCAST 0x0020, SO_ERROR/SO_SNDTIMEO/SO_RCVTIMEO, **SOMAXCONN 128** (matches IDF's `<net/if.h>`; upstream `sp_net_listen_all` passes it to listen(2) and the standalone cross-build has no `<net/if.h>`), sockaddr/sockaddr_storage, msghdr, send/recvmsg decls, socklen_t = u32. Values match lwip (`lwip/sockets.h`). SO_BROADCAST/PF_UNIX added for sp_io.c's `sp_sock_const` table (unknown names → -1 → NameError at runtime). |
| `sys/un.h` | replacement | struct sockaddr_un, SUN_LEN. AF_UNIX lives in sys/socket.h (value 1, identical in glibc/newlib). |
| `sys/ioctl.h` | replacement | TIOCGWINSZ/TIOCSWINSZ/TIOCOUTQ, ioctl decl. Decl-only; ioctl stub returns ENOSYS at runtime. |
| `sys/resource.h` | replacement | struct rusage, RUSAGE_SELF/CHILDREN. No RLIMIT_/PRIO_ (the runtime only reads rusage; getrusage stub returns ENOSYS). |
| `arpa/inet.h` | replacement | inet_ntop/inet_pton/inet_aton decls + prototypes for inet_addr/htonl/ntohl/htons/ntohs. |
| `netinet/in.h` | replacement | sockaddr_in with sin_len (lwip layout), INADDR_ANY, htonl/htons macros **without** `UL` suffixes after parenthesized sub-expressions, htonl/htons byte-swap macros with balanced parens. Fix round: original htons had an unbalanced paren and htonl had `UL` after `(x)` groups -- both illegal C. |
| `netinet/tcp.h` | replacement | TCP_NODELAY 0x01, TCP_KEEPIDLE etc. (lwip values). |
| `netdb.h` | replacement | addrinfo hints AI_PASSIVE 0x01, EAI_* codes incl. EAI_NONAME 200, getaddrinfo/freeaddrinfo/gethostbyname decls, NI_MAXHOST 64, **gai_strerror decl** (upstream `sp_net_listen_all` calls it; IDF leaves `LWIP_NETDB_HAS_GAI_STRERROR` off, so the shim provides the definition). Deliberately **omits** NI_NAMEREQD and getnameinfo so sp_net.c's reverse-lookup path compiles out. |
| `poll.h` | replacement | pollfd, POLLIN/POLLOUT/POLLERR/POLLHUP/POLLPRI, poll decl. Stub returns ENOSYS. |
| `stdio_ext.h` | replacement | `__freadahead` (missing in newlib's stdio_ext.h, sp_io.c needs it), plus __fbufsize family. |
| `ucontext.h` | replacement | ucontext_t for RV32IMAC with enough of the glibc layout for sp_fiber.c's non-ASM path; backed by `src/ucontext.S`. |

### src/ sources

| Source | Role |
|---|---|
| `sp_esp32_stub.c` | all POSIX-surface stubs: mmap (MAP_FAILED/ENOMEM → slab off, every GC block a malloc), madvise (ENOSYS, dead code), sigaction/sigaltstack/signal (ENOSYS), fork/popen/system (ENOSYS), getrusage (ENOSYS), ioctl/poll (ENOSYS), clock stubs, and `gai_strerror` (lwip's EAI_* values mapped to stable strings — IDF has no provider). Modeled on `lib/wasi/sp_wasi.c`: every unsupported call fails honestly; no fake success. |
| `ucontext.S` | hand-written RV32IMAC getcontext/swapcontext/setcontext for the fiber stack-swap path, gated like `lib/sp_fiber_ctx.h`'s SP_FIBER_ASM. |

## Governing design facts

- Cross gcc (riscv32-esp-elf 14.2.0) doesn't define `__linux__` → all
  Linux-only paths in the runtime compile out. `SP_THREADS` undefined →
  single-threaded paths.
- `mmap` → MAP_FAILED (ENOMEM) ⇒ slab allocator disabled (`sp_slab_on = 0`):
  every GC block is a malloc; fiber stacks fall back to
  `sp_main_stack_run` ("run it where we are") or raise FiberError;
  sp_iobuffer falls back to malloc. All honest ENOSYS/failed returns.
- Socket-family headers are declaration-only with lwip-matched values, so a
  future real-socket integration can swap stubs for lwip without header churn.
- ESP-IDF's real headers need the project's `sdkconfig.h` and cannot be used
  standalone; the shim is self-contained.

## Next up (M2+)

- [ ] `src/app_main_adapter.c`: FreeRTOS task trampoline calling the spinel
      main body on a pinned static stack (findings.md B1+B5)
- [ ] stdout → UART is free via ESP-IDF (B6)
- [ ] `idf.py` firmware link against `libspinel_rt_esp32.a`
- [ ] Optional `src/alloc_caps.c` (MALLOC_CAP_INTERNAL vs SPIRAM policy)
- [ ] Optional static-pool fiber-stack override (see mman.h notes) to enable
      real fibers instead of the malloc fallback
