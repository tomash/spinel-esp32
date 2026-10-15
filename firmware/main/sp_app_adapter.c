/*
 * sp_app_adapter.c -- spinel ESP32-C3 firmware adapter (M2)
 *
 * ESP-IDF's startup (components/esp_system/startup.c) never defines a
 * main(): the main task calls app_main(), then vTaskDelete(NULL). The
 * spinel-generated TU does define main(argc, argv) (it sets
 * _sp_main_argc/_sp_main_argv and calls sp_main_stack_run(_sp_main_body)),
 * so this adapter is a plain function call, not a POSIX handoff.
 *
 * On target the shim mmap is a MAP_FAILED stub, so sp_main_stack_run runs
 * the Ruby body in place -- on this task's stack (sized by
 * CONFIG_ESP_MAIN_TASK_STACK_SIZE, see sdkconfig.defaults).
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

/* Not including spinel_rt.h: it has non-inline definitions, and the
 * generated app.c (same component) already compiles them once. Just
 * declare what we call. */
int main(int argc, char **argv);

void app_main(void)
{
    /* The main task's core comes from CONFIG_ESP_MAIN_TASK_AFFINITY:
       core 0 by default; core 1 for apps whose sdkconfig override moves
       it (app/wifi_status_dualcore.sdkconfig.esp32). */
    ESP_LOGI("spinel", "spinel app_main: calling generated main() on core %d",
             xPortGetCoreID());
    /* argc=0/argv=NULL: no ARGV on target; spinel's TU only stashes them. */
    int rc = main(0, NULL);
    ESP_LOGI("spinel", "spinel app: main() returned %d", rc);
    /* startup.c's main task vTaskDelete(NULL)s right after we return. */
}