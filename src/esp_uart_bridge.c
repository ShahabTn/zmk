#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>
#include <string.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEY_COUNT 28

static uint16_t remap_keycodes[KEY_COUNT];
static char rx_buf[32];
static uint8_t rx_len;
static struct k_work_delayable uart_rx_work;

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

static uint16_t keycode_from_name(const char *name) {
    if (strcmp(name, "KC_A") == 0) {
        return 0x04;
    }
    if (strcmp(name, "KC_B") == 0) {
        return 0x05;
    }
    if (strcmp(name, "KC_C") == 0) {
        return 0x06;
    }
    if (strcmp(name, "NONE") == 0) {
        return 0;
    }
    return 0;
}

static void handle_esp_line(char *line) {
    unsigned int position = 0;
    char key_name[12] = {0};

    if (sscanf(line, "M %u %11s", &position, key_name) == 2 && position < KEY_COUNT) {
        remap_keycodes[position] = keycode_from_name(key_name);
        LOG_INF("ESP remap position %u to %s", position, key_name);
    }
}

static void poll_esp_uart(struct k_work *work) {
    const struct device *uart = esp_uart();
    uint8_t c;

    ARG_UNUSED(work);

    if (!device_is_ready(uart)) {
        k_work_schedule(&uart_rx_work, K_MSEC(20));
        return;
    }

    while (uart_poll_in(uart, &c) == 0) {
        if (c == '\n') {
            rx_buf[rx_len] = '\0';
            handle_esp_line(rx_buf);
            rx_len = 0;
        } else if (c != '\r' && rx_len < sizeof(rx_buf) - 1) {
            rx_buf[rx_len++] = c;
        }
    }

    k_work_schedule(&uart_rx_work, K_MSEC(10));
}

static int esp_uart_bridge_init(void) {
    k_work_init_delayable(&uart_rx_work, poll_esp_uart);
    k_work_schedule(&uart_rx_work, K_MSEC(100));
    send_esp_line("BOOT ZMK-BLE\n");
    return 0;
}

int esp_uart_bridge_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL) {
        return 0;
    }

    send_esp_key_event(ev->position, ev->state);

    if (ev->position < KEY_COUNT && remap_keycodes[ev->position] != 0) {
        raise_zmk_keycode_state_changed_from_encoded(remap_keycodes[ev->position], ev->state,
                                                     k_uptime_get());
        return ZMK_EV_EVENT_HANDLED;
    }

    return 0;
}

ZMK_LISTENER(esp_uart_bridge, esp_uart_bridge_listener);
ZMK_SUBSCRIPTION(esp_uart_bridge, zmk_position_state_changed);

SYS_INIT(esp_uart_bridge_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
