# app/thread_status.rb -- a Thread (802.15.4 mesh) node from Ruby.
#
# ESP32-C6 (or any chip with an IEEE 802.15.4 radio): starts ESP-IDF's
# OpenThread stack on the native radio, forms a new Thread network on its
# own, and reports what it becomes. Alone it takes the leader role; a second
# 802.15.4 board running this app with the same dataset joins as a router or
# child (dataset sharing is the next step). The board LED shows the role:
# off = detached/disabled, blinking = child/router, on = leader.
#
#   tools/board-flash.sh esp32_c6_devkitc app/thread_status.rb 60

module Thread802
  ffi_source <<~C
    #include <stdio.h>
    #include <string.h>
    #include "nvs_flash.h"
    #include "esp_event.h"
    #include "esp_netif.h"
    #include "esp_vfs_eventfd.h"
    #include "esp_openthread.h"
    #include "esp_openthread_lock.h"
    #include "esp_openthread_netif_glue.h"
    #include "esp_openthread_types.h"
    #include "openthread/dataset_ftd.h"
    #include "openthread/instance.h"
    #include "openthread/ip6.h"
    #include "openthread/thread.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"

    static int t_started;

    /* Bring up the stack (as examples/openthread/ot_cli does: eventfds for
       the netif, the OT task queue and the radio driver; native 802.15.4
       radio; NVS for the dataset), then form a fresh network named `name`.
       0 on success, negative step number on failure. */
    int thread_start(const char *name) {
        if (t_started) return 0;
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase(); e = nvs_flash_init();
        }
        if (e != ESP_OK) return -1;
        if (esp_event_loop_create_default() != ESP_OK) return -2;
        if (esp_netif_init() != ESP_OK) return -3;
        esp_vfs_eventfd_config_t efd = { .max_fds = 3 };
        if (esp_vfs_eventfd_register(&efd) != ESP_OK) return -4;
        static esp_openthread_config_t cfg = {
            .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
            .platform_config = {
                .radio_config = { .radio_mode = RADIO_MODE_NATIVE },
                .host_config = { .host_connection_mode = HOST_CONNECTION_MODE_NONE },
                .port_config = { .storage_partition_name = "nvs",
                                 .netif_queue_size = 10, .task_queue_size = 10 },
            },
        };
        if (esp_openthread_start(&cfg) != ESP_OK) return -5;

        esp_openthread_lock_acquire(portMAX_DELAY);
        otInstance *ot = esp_openthread_get_instance();
        otOperationalDataset ds;
        int rc = 0;
        if (otDatasetCreateNewNetwork(ot, &ds) != OT_ERROR_NONE) rc = -6;
        if (!rc) {
            size_t n = strlen(name);
            if (n > OT_NETWORK_NAME_MAX_SIZE) n = OT_NETWORK_NAME_MAX_SIZE;
            memset(&ds.mNetworkName, 0, sizeof(ds.mNetworkName));
            memcpy(ds.mNetworkName.m8, name, n);
            if (otDatasetSetActive(ot, &ds) != OT_ERROR_NONE) rc = -7;
        }
        if (!rc && otIp6SetEnabled(ot, true) != OT_ERROR_NONE) rc = -8;
        if (!rc && otThreadSetEnabled(ot, true) != OT_ERROR_NONE) rc = -9;
        esp_openthread_lock_release();
        if (!rc) t_started = 1;
        return rc;
    }

    /* 0 disabled, 1 detached, 2 child, 3 router, 4 leader (otDeviceRole) */
    int thread_role(void) {
        if (!t_started) return 0;
        esp_openthread_lock_acquire(portMAX_DELAY);
        int r = (int)otThreadGetDeviceRole(esp_openthread_get_instance());
        esp_openthread_lock_release();
        return r;
    }

    /* "name panid channel extpanid rloc16 meshlocal-eid" into buf */
    int thread_info(char *buf, int cap) {
        if (!t_started || cap < 1) return -1;
        esp_openthread_lock_acquire(portMAX_DELAY);
        otInstance *ot = esp_openthread_get_instance();
        otOperationalDataset ds;
        int rc = otDatasetGetActive(ot, &ds) == OT_ERROR_NONE ? 0 : -2;
        if (!rc) {
            const otMeshLocalPrefix *mlp = otThreadGetMeshLocalPrefix(ot);
            const otIp6Address *eid = otThreadGetMeshLocalEid(ot);
            char ml[OT_IP6_ADDRESS_STRING_SIZE];
            otIp6AddressToString(eid, ml, sizeof ml);
            const uint8_t *x = ds.mExtendedPanId.m8;
            snprintf(buf, cap, "%s 0x%04x %u %02x%02x%02x%02x%02x%02x%02x%02x 0x%04x %s",
                     ds.mNetworkName.m8, ds.mPanId, ds.mChannel,
                     x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7],
                     otThreadGetRloc16(ot), ml);
            (void)mlp;
        }
        esp_openthread_lock_release();
        return rc;
    }

    int thread_delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS((uint32_t)ms)); return 0; }
  C

  ffi_func :thread_start,    [:str], :int
  ffi_func :thread_role,     [], :int
  ffi_func :thread_info,     [:binstr, :int], :int
  ffi_func :thread_delay_ms, [:int], :int, blocking: true
  ffi_func :board_name,      [], :str
  ffi_func :board_led_init,  [], :int
  ffi_func :board_led_set,   [:int], :int
end

ROLES = ["disabled", "detached", "child", "router", "leader"]

puts "board:  #{Thread802.board_name}"
rc = Thread802.thread_start("spinel-thread")
if rc != 0
  puts "thread: start failed (#{rc})"
else
  puts "thread: stack up, forming network spinel-thread"
  Thread802.board_led_init
  last = -1
  tick = 0
  info_shown = false
  while true
    role = Thread802.thread_role
    if role != last
      puts "thread: role #{ROLES[role] || role} (#{tick / 2}s)"
      last = role
    end
    if role == 4 && !info_shown
      buf = "\0".b * 160
      if Thread802.thread_info(buf, 160) == 0
        f = buf.delete("\0").split(" ")
        puts "thread: network #{f[0]}  pan #{f[1]}  channel #{f[2]}"
        puts "thread: extpanid #{f[3]}  rloc16 #{f[4]}"
        puts "thread: mesh-local #{f[5]}"
        info_shown = true
      end
    end
    # LED: leader steady on, child/router blinking, otherwise off
    led = role == 4 ? 1 : (role >= 2 ? tick % 2 : 0)
    Thread802.board_led_set(led)
    Thread802.thread_delay_ms(500)
    tick += 1
  end
end
