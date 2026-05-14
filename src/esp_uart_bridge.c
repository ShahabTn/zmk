#define DT_DRV_COMPAT zmk_behavior_esp_remap

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>
#include <string.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <dt-bindings/zmk/keys.h>
#include <hal/nrf_power.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEY_COUNT 28
#define NRF_UF2_BOOTLOADER_MAGIC 0x57

static uint32_t remap_keycodes[KEY_COUNT];
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

static uint32_t keycode_from_name(const char *name) {
    if (strcmp(name, "KC_A") == 0 || strcmp(name, "A") == 0) {
        return A;
    }
    if (strcmp(name, "KC_B") == 0 || strcmp(name, "B") == 0) {
        return B;
    }
    if (strcmp(name, "KC_C") == 0 || strcmp(name, "C") == 0) {
        return C;
    }
    if (strcmp(name, "NONE") == 0) {
        return 0;
    }
    return 0;
}

static const char *keycode_name(uint32_t keycode) {
    if (keycode == A) {
        return "KC_A";
    }
    if (keycode == B) {
        return "KC_B";
    }
    if (keycode == C) {
        return "KC_C";
    }
    return "NONE";
}

static void send_stored_remap(unsigned int position) {
    char msg[28];
    snprintk(msg, sizeof(msg), "STORED M %u %s\n", position, keycode_name(remap_keycodes[position]));
    send_esp_line(msg);
}

static void enter_uf2_bootloader(void) {
    send_esp_line("ACK BOOTLOADER\n");
    k_sleep(K_MSEC(100));
    NRF_POWER->GPREGRET = NRF_UF2_BOOTLOADER_MAGIC;
    sys_reboot(SYS_REBOOT_COLD);
}

static void handle_esp_line(char *line) {
    unsigned int position = 0;
    char key_name[12] = {0};
    char rx_ack[40];

    snprintk(rx_ack, sizeof(rx_ack), "RX %s\n", line);
    send_esp_line(rx_ack);

    if (strcmp(line, "BOOTLOADER") == 0 || strcmp(line, "DFU") == 0) {
        enter_uf2_bootloader();
    } else if (sscanf(line, "M %u %11s", &position, key_name) == 2 && position < KEY_COUNT) {
        uint32_t keycode = keycode_from_name(key_name);
        remap_keycodes[position] = keycode;
        LOG_INF("ESP remap position %u to %s", position, key_name);

        char ack[24];
        snprintk(ack, sizeof(ack), "ACK M %u %s\n", position, keycode_name(keycode));
        send_esp_line(ack);
        send_stored_remap(position);
    } else if (sscanf(line, "Q %u", &position) == 1 && position < KEY_COUNT) {
        send_stored_remap(position);
    } else {
        send_esp_line("ERR BAD_CMD\n");
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

static int esp_remap_binding_changed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event, bool pressed) {
    uint32_t position = binding->param1;

    if (position >= KEY_COUNT) {
        return -EINVAL;
    }

    send_esp_key_event(position, pressed);
    char msg[32];
    snprintk(msg, sizeof(msg), "ACTIVE M %lu %s\n", (unsigned long)position,
             keycode_name(remap_keycodes[position]));
    send_esp_line(msg);

    if (remap_keycodes[position] != 0) {
        int ret = raise_zmk_keycode_state_changed_from_encoded(remap_keycodes[position],
                                                               pressed, event.timestamp);
        if (ret < 0) {
            LOG_WRN("Failed to raise remap keycode for position %lu: %d",
                    (unsigned long)position, ret);
            return ret;
        }
    }

    return 0;
}

static int esp_remap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    return esp_remap_binding_changed(binding, event, true);
}

static int esp_remap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return esp_remap_binding_changed(binding, event, false);
}

static const struct behavior_driver_api esp_remap_driver_api = {
    .binding_pressed = esp_remap_binding_pressed,
    .binding_released = esp_remap_binding_released,
};

static int esp_remap_init(const struct device *dev) { return 0; }

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
#define ESP_REMAP_INST(n)                                                                  \
    BEHAVIOR_DT_INST_DEFINE(n, esp_remap_init, NULL, NULL, NULL, POST_KERNEL,              \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &esp_remap_driver_api)

DT_INST_FOREACH_STATUS_OKAY(ESP_REMAP_INST)
#endif

SYS_INIT(esp_uart_bridge_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
