#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/peripheral.h>

#include <math.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LED_GPIO_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(gpio_leds)

BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(indicator_led)), "An alias for the indicator LED is not found for LED_INDICATOR");

static const struct device *led_dev = DEVICE_DT_GET(LED_GPIO_NODE_ID);
static const uint8_t led_idx = DT_NODE_CHILD_IDX(DT_ALIAS(indicator_led));

static bool initialized = false;

struct blink_item {
    uint16_t duration_ms;
    uint16_t sleep_ms;
    uint8_t count;
};

K_MSGQ_DEFINE(led_msgq, sizeof(struct blink_item), 16, 1);

#if IS_ENABLED(CONFIG_ZMK_BLE)
static void output_blink(void) {
    struct blink_item blink = { 0 };

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint8_t profile_index = zmk_ble_active_profile_index();
    if (zmk_ble_active_profile_is_connected()) {
        LOG_INF("Profile %d connected, blinking once", profile_index);
        blink.duration_ms = CONFIG_LED_INDICATOR_BLINK_MS * 3;
        blink.count = 1;
    } else if (zmk_ble_active_profile_is_open()) {
        LOG_INF("Profile %d open, slow blinking", profile_index);
        blink.duration_ms = CONFIG_LED_INDICATOR_BLINK_MS * 2;
        blink.count = 3;
    } else {
        LOG_INF("Profile %d not connected, fast blinking", profile_index);
        blink.duration_ms = CONFIG_LED_INDICATOR_BLINK_MS;
        blink.count = 3;
    }
#else
    if (zmk_split_bt_peripheral_is_connected()) {
        LOG_INF("Peripheral connected, blinking once");
        blink.duration_ms = CONFIG_LED_INDICATOR_BLINK_MS * 3;
        blink.count = 1;
    } else {
        LOG_INF("Peripheral not connected, fast blinking");
        blink.duration_ms = CONFIG_LED_INDICATOR_BLINK_MS;
        blink.count = 3;
    }
#endif

    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
}

static int led_output_listener_cb(const zmk_event_t *eh) {
    if (initialized) {
        output_blink();
    }
    return 0;
}

ZMK_LISTENER(led_output_listener, led_output_listener_cb);
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_SUBSCRIPTION(led_output_listener, zmk_ble_active_profile_changed);
#else
ZMK_SUBSCRIPTION(led_output_listener, zmk_split_peripheral_status_changed);
#endif
#endif // IS_ENABLED(CONFIG_ZMK_BLE)

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static int led_battery_listener_cb(const zmk_event_t *eh) {
    if (!initialized) {
        return 0;
    }

    uint8_t battery_level = as_zmk_battery_state_changed(eh)->state_of_charge;

    if (battery_level <= CONFIG_LED_INDICATOR_BATTERY_LEVEL_CRITICAL) {
        LOG_INF("Battery level %d, blinking for critical", battery_level);

        struct blink_item blink = {.duration_ms = CONFIG_LED_INDICATOR_BLINK_MS,
                                   .count = 2};
        k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
    }
    return 0;
}

ZMK_LISTENER(led_battery_listener, led_battery_listener_cb);
ZMK_SUBSCRIPTION(led_battery_listener, zmk_battery_state_changed);
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

extern void led_process_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    while (true) {
        struct blink_item blink;
        k_msgq_get(&led_msgq, &blink, K_FOREVER);
        LOG_DBG("Got a blink item from msgq, duration %d", blink.duration_ms);

        for (int i = 0; i < blink.count; i++) {
            led_on(led_dev, led_idx);

            k_sleep(K_MSEC(blink.duration_ms));

            led_off(led_dev, led_idx);

            if (blink.sleep_ms > 0) {
                k_sleep(K_MSEC(blink.sleep_ms));
            } else {
                k_sleep(K_MSEC(CONFIG_LED_INDICATOR_INTERVAL_MS));
            }
        }
    }
}

// define led_process_thread with stack size 1024, start running it 100 ms after boot
K_THREAD_DEFINE(led_process_tid, 1024, led_process_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
                0, 100);

extern void led_init_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    LOG_INF("Indicating initial battery status");

    struct blink_item blink = { 0 };

    uint8_t battery_level = zmk_battery_state_of_charge();
    while (battery_level == 0) {
        k_sleep(K_MSEC(100));
        battery_level = zmk_battery_state_of_charge();
    };

    blink.duration_ms = CONFIG_LED_INDICATOR_BLINK_MS;
    blink.count = round(battery_level / 10);
    LOG_INF("Battery level %d, blinking %d time(s)", battery_level, blink.count);

    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);

    k_sleep(K_MSEC((CONFIG_LED_INDICATOR_BLINK_MS + CONFIG_LED_INDICATOR_INTERVAL_MS) * (blink.count + 1)));
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

#if IS_ENABLED(CONFIG_ZMK_BLE)
    LOG_INF("Indicating initial connectivity status");
    output_blink();
#endif // IS_ENABLED(CONFIG_ZMK_BLE)

    initialized = true;
    LOG_INF("Finished initializing LED widget");
}

K_THREAD_DEFINE(led_init_tid, 1024, led_init_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
                0, 200);