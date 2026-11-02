/*
 * A UART bridge on the ESP32-S3 host to its soldered BT satellite (ESP32-WROOM, bare module),
 * so that esptool on the PC reaches the satellite's ROM loader through the host's COM port.
 * Pin map: boards/s3_player/board.json (uart_sat TX 11 / RX 12) and docs/S3_PORT.md, "Wiring"
 * (the satellite's EN on GPIO13, its IO0 on GPIO2).
 *
 * Every byte is passed both ways at 115200. The satellite is left alone until the PC asks:
 * "#SAT-DL#" resets it into its ROM loader (IO0 low through the reset), "#SAT-RUN#" into its
 * application. Every read blocks for at least a tick: a zero timeout made the forwarding task
 * spin and starve the other direction.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PC_UART   UART_NUM_0   /* the board's COM port (CH343) on GPIO43/44 */
#define PC_TX     43
#define PC_RX     44
#define SAT_UART  UART_NUM_1
#define SAT_TX    11           /* to the satellite's RXD0 (GPIO3) */
#define SAT_RX    12           /* from the satellite's TXD0 (GPIO1) */
#define SAT_EN    13           /* the satellite's EN: low = reset */
#define SAT_IO0   2            /* the satellite's IO0: low through a reset = ROM loader */
#define BAUD      115200

typedef struct { const char *word; bool loader; size_t at; } cmd_t;
static cmd_t s_cmds[] = { {"#SAT-DL#", true, 0}, {"#SAT-RUN#", false, 0} };

static void sat_reset(bool loader)
{
    /* Driven only from here on: until the first command both pins float, and the module's own
     * pull-ups hold the satellite running. */
    gpio_set_level(SAT_IO0, loader ? 0 : 1);
    gpio_set_level(SAT_EN, 0);
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << SAT_EN) | (1ULL << SAT_IO0),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(100));   /* EN has 1 uF at the module */
    gpio_set_level(SAT_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(SAT_IO0, 1);
}

static void sat_to_pc(void *arg)
{
    static uint8_t buf[1024];
    for (;;) {
        const int n = uart_read_bytes(SAT_UART, buf, sizeof buf, pdMS_TO_TICKS(2) + 1);
        if (n > 0) uart_write_bytes(PC_UART, buf, n);
    }
}

void app_main(void)
{
    const uart_config_t cfg = {
        .baud_rate = BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(PC_UART, 8192, 8192, 0, NULL, 0);
    uart_param_config(PC_UART, &cfg);
    uart_set_pin(PC_UART, PC_TX, PC_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(SAT_UART, 8192, 8192, 0, NULL, 0);
    uart_param_config(SAT_UART, &cfg);
    uart_set_pin(SAT_UART, SAT_TX, SAT_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    xTaskCreate(sat_to_pc, "sat2pc", 3072, NULL, 10, NULL);

    static uint8_t buf[1024];
    for (;;) {
        const int n = uart_read_bytes(PC_UART, buf, sizeof buf, pdMS_TO_TICKS(2) + 1);
        if (n <= 0) continue;
        uart_write_bytes(SAT_UART, buf, n);
        for (int i = 0; i < n; i++) {
            for (size_t c = 0; c < sizeof s_cmds / sizeof s_cmds[0]; c++) {
                cmd_t *k = &s_cmds[c];
                k->at = buf[i] == (uint8_t)k->word[k->at] ? k->at + 1 : (buf[i] == (uint8_t)k->word[0] ? 1 : 0);
                if (k->word[k->at] == '\0') {
                    const char *say = k->loader ? "\r\n[bridge] loader\r\n" : "\r\n[bridge] run\r\n";
                    uart_write_bytes(PC_UART, say, strlen(say));
                    sat_reset(k->loader);
                    k->at = 0;
                }
            }
        }
    }
}
