#define DT_DRV_COMPAT zmk_behavior_uart_ping

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int uart_ping_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart1));

    if (!device_is_ready(uart)) {
        LOG_WRN("uart1 is not ready");
        return -ENODEV;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    static const char msg[] = "PING-R\n";
#else
    static const char msg[] = "PING-L\n";
#endif

    for (int i = 0; i < sizeof(msg) - 1; i++) {
        uart_poll_out(uart, msg[i]);
    }

    return 0;
}

static int uart_ping_keymap_binding_released(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return 0;
}

static const struct behavior_driver_api uart_ping_driver_api = {
    .binding_pressed = uart_ping_keymap_binding_pressed,
    .binding_released = uart_ping_keymap_binding_released,
};

static int uart_ping_init(const struct device *dev) { return 0; }

#define UART_PING_INST(n)                                                                   \
    BEHAVIOR_DT_INST_DEFINE(n, uart_ping_init, NULL, NULL, NULL, POST_KERNEL,              \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &uart_ping_driver_api)

DT_INST_FOREACH_STATUS_OKAY(UART_PING_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
