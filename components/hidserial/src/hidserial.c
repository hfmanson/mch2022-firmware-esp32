/*
 * Copyright (c) 2026 Henri Manson
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include <hardware.h>
#include "driver/uart.h"
#include "soc/uart_reg.h"

#include "hidserial.h"

#define MY_UART 0
#define RD_BUF_SIZE 256
#define UART_EMPTY_THRESH_DEFAULT  (10)
#define UART_FULL_THRESH_DEFAULT  (120)
#define UART_TOUT_THRESH_DEFAULT   (10)
#define UART_CLKDIV_FRAG_BIT_WIDTH  (3)
#define UART_TOUT_REF_FACTOR_DEFAULT (UART_CLK_FREQ/(REF_CLK_FREQ<<UART_CLKDIV_FRAG_BIT_WIDTH))
#define UART_TX_IDLE_NUM_DEFAULT   (0)
#define UART_PATTERN_DET_QLEN_DEFAULT (10)
#define UART_MIN_WAKEUP_THRESH      (2)

#define HID_MAX_KEYS 6

static const char *TAG = "main";

QueueHandle_t uart_queue;

typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[HID_MAX_KEYS];
} hid_report_t;

static hid_report_t prev;

static void on_key(bool pressed, uint8_t key)
{
    int input_button = -1;
    ESP_LOGI(TAG, "on_key: pressed = %d, key = %02X", pressed, key);

    switch (key) {
        case HID_KEY_ENTER:
            input_button = RP2040_INPUT_BUTTON_ACCEPT;
            break;
        case HID_KEY_ESCAPE:
            input_button = RP2040_INPUT_BUTTON_BACK;
            break;
        case HID_KEY_LEFT:
            input_button = RP2040_INPUT_JOYSTICK_LEFT;
            break;
        case HID_KEY_RIGHT:
            input_button = RP2040_INPUT_JOYSTICK_RIGHT;
            break;
        case HID_KEY_UP:
            input_button = RP2040_INPUT_JOYSTICK_UP;
            break;
        case HID_KEY_DOWN:
            input_button = RP2040_INPUT_JOYSTICK_DOWN;
            break;
        case HID_KEY_KP_ENTER:
            input_button = RP2040_INPUT_JOYSTICK_PRESS;
            break;
        case HID_KEY_F1:
            input_button = RP2040_INPUT_BUTTON_HOME;
            break;
        case HID_KEY_F2:
            input_button = RP2040_INPUT_BUTTON_MENU;
            break;
        case HID_KEY_F3:
            input_button = RP2040_INPUT_BUTTON_SELECT;
            break;
        case HID_KEY_F4:
            input_button = RP2040_INPUT_BUTTON_START;
            break;
        default:
            input_button = -1;
    }
    if (input_button != -1) {
        rp2040_input_message_t message = {
            (uint8_t) input_button,
            pressed,
        };
        xQueueSend(get_rp2040()->queue, &message, (TickType_t) 0);
    }
}

static void on_modifier(bool pressed, uint8_t key)
{
}

static int key_in_report(const hid_report_t *r, uint8_t key)
{
    for (int i = 0; i < HID_MAX_KEYS; i++)
        if (r->keys[i] == key)
            return 1;
    return 0;
}

void hid_process(const hid_report_t *curr)
{
    // 1. Detect released keys
    for (int i = 0; i < HID_MAX_KEYS; i++) {
        uint8_t key = prev.keys[i];
        if (key != 0 && !key_in_report(curr, key)) {
            // key was present before, but not now
            on_key(false, key);
        }
    }

    // 2. Detect pressed keys
    for (int i = 0; i < HID_MAX_KEYS; i++) {
        uint8_t key = curr->keys[i];
        if (key != 0 && !key_in_report(&prev, key)) {
            // key is new
            on_key(true, key);
        }
    }

    // 3. Modifier changes
    uint8_t prev_mod = prev.modifier;
    uint8_t curr_mod = curr->modifier;

    uint8_t changed = prev_mod ^ curr_mod;

    if (changed) {
        for (uint8_t bit = 1; bit != 0; bit <<= 1) {
            if (!(changed & bit))
                continue;

            if (curr_mod & bit)
                on_modifier(true, bit);
            else
                on_modifier(false, bit);
        }
    }
}


static void serial_kbd_cb(const uint8_t *r, size_t len) {
    // r[0] = modifiers
    // r[1] = reserved
    // r[2..7] = keycodes
     ESP_LOGI(TAG, "serial_kbd_cb: %02X %02X %02X %02X %02X %02X %02X %02X",
     r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
     hid_report_t *current = (hid_report_t *) r;
     hid_process(current);
     prev = *current;
}

static void serial_mouse_cb(const uint8_t *r, size_t len) {
}

static void uartTask(void *pvParameter) {
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);

    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    ESP_LOGI(TAG, "siz: %d", event.size);
                    uart_read_bytes(MY_UART, dtmp, event.size, portMAX_DELAY);
#ifdef UART_DEBUG
					for (int i = 0; i < event.size; i++) {
						ESP_LOGI(TAG, "data[%2d] = %02X", i, dtmp[i]);
					}
#endif
					serial_kbd_cb(dtmp, event.size);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(MY_UART);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(MY_UART);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                
                    break;
                //Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void hid_uart_init() {
    //uart_param_config(MY_UART, &uart_config);   //Configure the uart hardware
    //uart_set_pin(MY_UART, CONFIG_DRIVER_FSOVERBUS_UART_TX, CONFIG_DRIVER_FSOVERBUS_UART_RX, CONFIG_DRIVER_FSOVERBUS_UART_CTS, -1); //Change pins
	uart_config_t uartcfg = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.rx_flow_ctrl_thresh = 0
	};
	uart_param_config(0, &uartcfg);
	uart_driver_install(0, RD_BUF_SIZE, RD_BUF_SIZE, 40, &uart_queue, 0);

    uart_intr_config_t uart_intr = {
        .intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M
                            | UART_RXFIFO_TOUT_INT_ENA_M
                            | UART_FRM_ERR_INT_ENA_M
                            | UART_RXFIFO_OVF_INT_ENA_M
                            | UART_BRK_DET_INT_ENA_M
                            | UART_PARITY_ERR_INT_ENA_M,
        .rxfifo_full_thresh = 64,
        .rx_timeout_thresh = UART_TOUT_THRESH_DEFAULT,
        .txfifo_empty_intr_thresh = UART_EMPTY_THRESH_DEFAULT
    };
    uart_intr_config(MY_UART, &uart_intr);
    xTaskCreatePinnedToCore(uartTask, "fsoverbus_uart", 16000, NULL, 100, NULL, 0);

}
