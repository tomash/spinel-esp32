/*
 * sp_ota_adapter.c -- M6: OTA flash-write adapter for the Ruby app.
 *
 * Thin wrappers around esp_ota_* so the Ruby code (app/wifi_status.rb, FFI)
 * can drive the download protocol itself: Ruby owns HTTP + chunking + magic
 * verification over spinel's TCPSocket; C only owns the flash machinery.
 * No network I/O happens here -- Ruby hands one buffer per chunk, one FFI
 * call per chunk, so all concurrency stays out of C (plan D3: single-task
 * Ruby; the OTA handle is touched only from the Ruby task).
 *
 * All entry points return 0 on success, negative errno-ish otherwise
 * (same convention as sp_app_adapter.c callers / app FFI funcs).
 */

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "string.h"
#include "stdio.h"

/* Single-instance OTA state. Mutex-free: only the Ruby task may call
   these, and the Ruby body is one cooperative loop (no threads). */
static esp_ota_handle_t s_ota_handle;
static const esp_partition_t *s_ota_target;
static int s_ota_in_progress;

const char *esp32_ota_partition_name(void)
{
    return s_ota_target ? s_ota_target->label : "(none)";
}

int esp32_ota_begin(void)
{
    if (s_ota_in_progress) {
        /* A previous download died mid-stream without ota_end/abort:
           release the stale handle first (abandon, don't set boot). */
        esp_ota_abort(s_ota_handle);
        s_ota_in_progress = 0;
    }
    /* Next usable update partition = the one not currently running. */
    s_ota_target = esp_ota_get_next_update_partition(NULL);
    if (s_ota_target == NULL) {
        printf("OTA: no update partition available\n");
        return -1;
    }
    /* OTA_WITH_SEQUENTIAL_WRITES (as native_ota_example): the image length
       comes from the HTTP stream, not from Content-Length (which we do
       not trust blindly). Unlike OTA_SIZE_UNKNOWN this does NOT erase the
       whole slot up front inside esp_ota_begin (a single long blocking
       call in the Ruby cooperative loop); instead need_erase is set and
       each sector is erased on demand inside esp_ota_write -- the erase
       cost is spread across the download. */
    esp_err_t err = esp_ota_begin(s_ota_target, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle);
    if (err != ESP_OK) {
        printf("OTA: esp_ota_begin on %s failed: %d\n", s_ota_target->label, err);
        s_ota_target = NULL;
        return -2;
    }
    s_ota_in_progress = 1;
    printf("OTA: begin writing slot %s (sequential erase)\n", s_ota_target->label);
    return 0;
}

/* Version guard from native_ota_example, split out so the Ruby download
   loop can call it on the first bytes BEFORE esp32_ota_begin: buf holds
   the first >= 80 bytes of the image. Layout there: 24-byte image header,
   8-byte segment header, then the esp_app_desc_t (216 bytes) whose
   magic_word sits at +0 and version[32] at +16 (image offset 48..80).
   The example memcpy's the whole descriptor and demands 248 bytes; we
   only need the version, so 80 bytes suffice -- and that is exactly what
   the Ruby loop accumulates before calling. Refuses to continue when the
   incoming version matches (a) the running image -- pointless reflash,
   or (b) esp_ota_get_last_invalid_partition() -- a version that already
   failed to prove life after boot. Return 0 = proceed; <0 = refuse. */
int esp32_ota_check_version(const uint8_t *buf, size_t len)
{
    const size_t off_desc = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    const size_t off_ver = off_desc + 16;   /* skip magic+secure_version+reserv1 */
    if (buf == NULL || len < off_ver + 32) return -1;
    uint32_t magic;
    memcpy(&magic, buf + off_desc, sizeof(magic));
    if (magic != ESP_APP_DESC_MAGIC_WORD) {
        printf("OTA: bad app_desc magic 0x%08x\n", (unsigned)magic);
        return -1;
    }
    char new_ver[33];
    memcpy(new_ver, buf + off_ver, 32);
    new_ver[32] = '\0';
    printf("OTA: new image version: %s\n", new_ver);

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t run_info;
    if (esp_ota_get_partition_description(running, &run_info) == ESP_OK) {
        printf("OTA: running version: %s\n", run_info.version);
        if (memcmp(new_ver, run_info.version, 32) == 0) {
            printf("OTA: refusing -- new version equals running version\n");
            return -2;
        }
    }
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    esp_app_desc_t inv_info;
    if (invalid && esp_ota_get_partition_description(invalid, &inv_info) == ESP_OK &&
        memcmp(new_ver, inv_info.version, 32) == 0) {
        printf("OTA: refusing -- version %s previously failed/rolled back\n",
               inv_info.version);
        return -3;
    }
    return 0;
}

int esp32_ota_write(const uint8_t *buf, size_t len)
{
    if (!s_ota_in_progress) return -1;
    if (buf == NULL || len == 0) return -2;
    esp_err_t err = esp_ota_write(s_ota_handle, buf, len);
    if (err != ESP_OK) {
        printf("OTA: esp_ota_write failed: %d\n", err);
        return -3;
    }
    return 0;
}

/* abort != 0 -> abandon the download (bad image / truncated stream).
   abort == 0 -> finalize; the new image boots only after the next
   esp_restart, and only marks itself valid once it has proven life. */
int esp32_ota_end(int abort)
{
    if (!s_ota_in_progress) return -1;
    s_ota_in_progress = 0;
    if (abort) {
        esp_ota_abort(s_ota_handle);
        printf("OTA: aborted on %s\n", s_ota_target ? s_ota_target->label : "?");
        s_ota_target = NULL;
        return 0;
    }
    esp_err_t err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        printf("OTA: esp_ota_end failed: %d\n", err);
        s_ota_target = NULL;
        return -2;
    }
    err = esp_ota_set_boot_partition(s_ota_target);
    if (err != ESP_OK) {
        printf("OTA: set_boot_partition failed: %d\n", err);
        s_ota_target = NULL;
        return -3;
    }
    printf("OTA: image sealed, boot slot %s on next reboot\n",
           s_ota_target->label);
    return 0;
}

/* Which slot is running: 0 = ota_0, 1 = ota_1, -1 = not an OTA slot
   (factory -- only true before the one-time re-partition reflash). */
int esp32_ota_running_slot(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *o0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *o1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (o0 && run->address == o0->address) return 0;
    if (o1 && run->address == o1->address) return 1;
    return -1;
}

/* Proof-of-life latch for the bootloader's rollback: the freshly
   booted slot calls this after the status server answered one request.
   No-op semantics after the first call (bootloader clamps to the same
   state it is already in). */
int esp32_ota_mark_valid(void)
{
    esp_ota_mark_app_valid_cancel_rollback();
    printf("OTA: running image marked valid (rollback cancelled)\n");
    return 0;
}

int esp32_ota_reboot(void)
{
    printf("OTA: rebooting now\n");
    fflush(stdout);
    esp_restart();
    return 0; /* never reached */
}
