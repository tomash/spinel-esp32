# patches/ — carried diffs against vendor/spinel

Series-style, numbered by milestone, applied with tools/apply-patches.sh
(git apply --3way against vendor/spinel @ SPINEL_PIN). Keep the set minimal:
everything that can be a new file in shim/esp32/ should be; only genuine
modifications of upstream runtime sources go here.

Expected series (drafted in findings.md, none needed for the host pipeline):

- 0010-fiber-static-main-stack.patch   — sp_main_stack_run: static buffer
  instead of mmap+guard (B1). Alternative: keep mmap semantics behind a
  weak symbol and let shim/esp32 override sp_main_stack_run entirely
  (preferred: zero upstream diff).
- 0020-fiber-ctx-riscv32.patch        — SP_FIBER_ASM for __riscv (B2),
  unless the asm can live in shim/esp32/src/sp_ctx_riscv.c and link-time
  override works (preferred, same trick).
- 0030-signal-machinery-gate.patch    — compile out sigaction machinery
  when SP_NO_SIGNALS is defined (B3). Or stub sigaction in the shim so no
  upstream change is needed (wasi precedent says: stub).

Rule of thumb (from the wasm32-wasi precedent): the upstream runtime already
links clean over a target whose libc lacks the whole POSIX surface, provided
the headers exist and the missing functions are defined to fail. Prefer shim
files + weak-symbol overrides over patching upstream. Every patch here should
be justified in its header against that rule.
