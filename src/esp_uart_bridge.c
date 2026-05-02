#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static const struct device *esp_uart(void) {
    return DEVICE_DT_GET(DT_NODELABEL(uart1));
}

static void send_esp_line(const char *msg) {
    const struct device *uart = esp_uart();

    if (!device_is_ready(uart)) {
        LOG_WRN("uart1 is not ready");
        return;
    }

    for (int i = 0; msg[i] != '\0'; i++) {
        uart_poll_out(uart, msg[i]);
    }
}

static void send_esp_key_event(uint32_t position, bool pressed) {
    const struct device *uart = esp_uart();

    if (!device_is_ready(uart)) {
        LOG_WRN("uart1 is not ready");
        return;
    }

    char msg[20];
    int len = snprintk(msg, sizeof(msg), "K %lu %u\n", (unsigned long)position,
                       pressed ? 1 : 0);

    for (int i = 0; i < len; i++) {
        uart_poll_out(uart, msg[i]);
    }
}

static int esp_uart_bridge_init(void) {
    send_esp_line("BOOT ZMK-BLE\n");
    return 0;
}

int esp_uart_bridge_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL) {
        return 0;
    }

    send_esp_key_event(ev->position, ev->state);
    return 0;
}

ZMK_LISTENER(esp_uart_bridge, esp_uart_bridge_listener);
ZMK_SUBSCRIPTION(esp_uart_bridge, zmk_position_state_changed);

SYS_INIT(esp_uart_bridge_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
