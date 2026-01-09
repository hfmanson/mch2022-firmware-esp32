#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <pax_codecs.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "appfs_wrapper.h"
#include "audio.h"
#include "bootscreen.h"
#include "driver/uart.h"
#include "efuse.h"
#include "esp32/rom/crc.h"
#include "esp_ota_ops.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "factory_test.h"
#include "filesystems.h"
#include "fpga_download.h"
#include "fpga_test.h"
#include "graphics_wrapper.h"
#include "gui_element_header.h"
#include "hardware.h"
#include "managed_i2c.h"
#include "menu.h"
#include "menus/start.h"
#include "msc.h"
#include "pax_gfx.h"
#include "rp2040.h"
#include "rp2040_updater.h"
#include "rp2040bl.h"
#include "rtc_memory.h"
#include "sao_eeprom.h"
#include "settings.h"
#include "system_wrapper.h"
#include "webusb.h"
#include "wifi_cert.h"
#include "wifi_connection.h"
#include "wifi_defaults.h"
#include "wifi_ota.h"
#include "ws2812.h"
#include "soc/uart_reg.h"

extern const uint8_t logo_screen_png_start[] asm("_binary_logo_screen_png_start");
extern const uint8_t logo_screen_png_end[] asm("_binary_logo_screen_png_end");

static const char* TAG = "main";

void display_fatal_error(const char* line0, const char* line1, const char* line2, const char* line3) {
    pax_buf_t*        pax_buffer = get_pax_buffer();
    const pax_font_t* font       = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xa85a32);
    if (line0 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 23, 0, 20 * 0, line0);
    if (line1 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 1, line1);
    if (line2 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 2, line2);
    if (line3 != NULL) pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 18, 0, 20 * 3, line3);
    display_flush();
}

void display_rp2040_crashed_message() {
    pax_buf_t*        pax_buffer = get_pax_buffer();
    const pax_font_t* font       = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xf5ec42);
    pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Oops...");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 2, "The co-processor crashed, causing");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 3, "the badge to be restarted.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 5, "Help us debug the problem by");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 6, "submitting a ticket on Github");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 7, "explaining what caused the crash.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 9, "You can find the repository at:");
    pax_draw_text(pax_buffer, 0xFF000000, font, 12, 0, 20 * 10, "https://github.com/badgeteam\n/mch2022-firmware-rp2040    Press A to continue.");
    display_flush();
    wait_for_button();
}

bool display_rp2040_flash_lock_warning() {
    pax_buf_t*        pax_buffer = get_pax_buffer();
    const pax_font_t* font       = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xf5ec42);
    pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Flashing attempt detected");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 1, "Hi there developer!");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 2, "You tried to overwrite the launcher");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 3, "firmware, are you sure you want to");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 4, "do that? We recommend you to");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 5, "install your app using the");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 6, "webusb_push.py tool.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 7, "Check out https://docs.badge.team");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 8, "for more information.");
    pax_draw_text(pax_buffer, 0xFF000000, font, 12, 0, 20 * 10,
                  "You can disable this protection by pressing A\nTo continue starting without disabling the protection\npress B.");
    display_flush();
    return wait_for_button();
}

void display_rp2040_debug_message() {
    pax_buf_t*        pax_buffer = get_pax_buffer();
    const pax_font_t* font       = pax_font_saira_regular;
    pax_noclip(pax_buffer);
    pax_background(pax_buffer, 0xf5ec42);
    pax_draw_text(pax_buffer, 0xFF000000, font, 23, 0, 20 * 0, "Debug mode");
    pax_draw_text(pax_buffer, 0xFF000000, font, 18, 0, 20 * 2, "Co-processor is in debug mode");
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(500));
}

void stop() {
    ESP_LOGW(TAG, "*** HALTED ***");
    gpio_set_direction(GPIO_SD_PWR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_SD_PWR, 1);
    uint8_t led_off[15]  = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t led_red[15]  = {0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0};
    uint8_t led_red2[15] = {0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0, 0, 0xFF, 0};
    while (true) {
        ws2812_send_data(led_red2, sizeof(led_red2));
        vTaskDelay(pdMS_TO_TICKS(200));
        ws2812_send_data(led_red, sizeof(led_red));
        vTaskDelay(pdMS_TO_TICKS(200));
        ws2812_send_data(led_off, sizeof(led_off));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

const char* fatal_error_str = "A fatal error occured";
const char* reset_board_str = "Reset the board to try again";

static void audio_player_task(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(500));
    play_bootsound();
    uint8_t leds[15] = {0};
    for (uint8_t part = 0; part < 50; part++) {
        // Center of the kite: green.
        leds[3 * 0 + 1] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Left of the kite: red.
        leds[3 * 1 + 0] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Top of the kit: blue.
        leds[3 * 2 + 2] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Right of the kite: yellow.
        leds[3 * 3 + 0] = part;
        leds[3 * 3 + 1] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Bottom of the kite: blue.
        leds[3 * 4 + 2] = part;
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (uint8_t part = 0; part < 50; part++) {
        // Center of the kite: green.
        leds[3 * 0 + 1] = 49 - part;
        // Left of the kite: red.
        leds[3 * 1 + 0] = 49 - part;
        // Top of the kit: blue.
        leds[3 * 2 + 2] = 49 - part;
        // Right of the kite: yellow.
        leds[3 * 3 + 0] = 49 - part;
        leds[3 * 3 + 1] = 49 - part;
        // Bottom of the kite: blue.
        leds[3 * 4 + 2] = 49 - part;

        // Send the LED data.
        ws2812_send_data(leds, sizeof(leds));
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}

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

QueueHandle_t uart_queue;

static void handle_data(xQueueHandle button_queue, char *data, int size)
{
	int result = -1;
	switch (size) {
	case 1:
		switch (data[0]) {
		case '\r':
			result = RP2040_INPUT_BUTTON_ACCEPT;
			break;
		case 27:
			result = RP2040_INPUT_BUTTON_BACK;
			break;
		}
		break;
	case 3:
		// cursor keys
		if (data[0] == 27 && data[1] == '[') {
			switch (data[2]) {
			case 'D':
				result = RP2040_INPUT_JOYSTICK_LEFT;
				break;
			case 'C':
				result = RP2040_INPUT_JOYSTICK_RIGHT;
				break;
			case 'A':
				result = RP2040_INPUT_JOYSTICK_UP;
				break;
			case 'B':
				result = RP2040_INPUT_JOYSTICK_DOWN;
				break;
			}
		}
		break;
	case 5:
		// function keys F1 - F4
		if (data[0] == 27 && data[1] == '[' && data[2] == '1' && data[4] == 0x7E) {
			switch (data[3]) {
			case '1':
				result = RP2040_INPUT_BUTTON_HOME;
				break;
			case '2':
				result = RP2040_INPUT_BUTTON_MENU;
				break;
			case '3':
				result = RP2040_INPUT_BUTTON_SELECT;
				break;
			case '4':
				result = RP2040_INPUT_BUTTON_START;
				break;
			}
		}
		break;
	}
	if (result != -1) {
		rp2040_input_message_t message = {
			(uint8_t) result,
			true,
		};
		xQueueSend(button_queue, &message, (TickType_t) 10);
		message.state = false;
		xQueueSend(button_queue, &message, (TickType_t) 10);
	}
}

static void uartTask(void *pvParameter) {
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    RP2040* rp2040 = get_rp2040();

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
					handle_data(rp2040->queue, (char *) dtmp, event.size);
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

static void my_uart_init() {
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

void app_main(void) {
    esp_err_t res;

    audio_init();

    const esp_app_desc_t* app_description = esp_ota_get_app_description();
    printf("BADGE.TEAM %s launcher firmware v%s\r\n", app_description->project_name, app_description->version);

    /* Initialize hardware */

    efuse_protect();

    if (bsp_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize basic board support functions");
        esp_restart();
    }

    /* Initialize LCD screen */
    pax_buf_t* pax_buffer = get_pax_buffer();
    display_boot_screen("Starting...");

    /* Enable power to the LEDs and the SD card */
    res = gpio_set_direction(GPIO_SD_PWR, GPIO_MODE_OUTPUT);
    if (res != ESP_OK) stop();
    res = gpio_set_level(GPIO_SD_PWR, true);
    if (res != ESP_OK) stop();

    /* Initialize the LEDs */
    ws2812_init(GPIO_LED_DATA, 150);
    const uint8_t led_off[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ws2812_send_data(led_off, sizeof(led_off));

    /* Start NVS */
    res = nvs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", res);
        display_fatal_error(fatal_error_str, "NVS failed to initialize", "Flash may be corrupted", NULL);
        stop();
    }

    nvs_handle_t handle;
    res = nvs_open("system", NVS_READWRITE, &handle);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", res);
        display_fatal_error(fatal_error_str, "Failed to open NVS namespace", "Flash may be corrupted", reset_board_str);
        stop();
    }

    /* Initialize RP2040 co-processor */
    if (bsp_rp2040_init() != ESP_OK) {
        // This error state happens when
        //  - The I2C bus gets shorted out
        //  - The RP2040 does not boot
        ESP_LOGE(TAG, "Failed to initialize the RP2040 co-processor");
        const pax_font_t* font = pax_font_saira_regular;
        pax_background(pax_buffer, 0xa85a32);
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 23, 0, 20 * 0, "Communication error");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 1, "The ESP32 is unable to communicate");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 2, "with the RP2040 co-processor, this");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 3, "could be caused by a problem with");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 4, "the I2C bus, which in turn can be");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 5, "caused by a connected SAO board or");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 6, "a connected QWIIC / Stemma QT device");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 8, "Please check the I2C bus and power");
        pax_draw_text(pax_buffer, 0xFFFFFFFF, font, 16, 0, 20 * 9, "cycle the badge to try again");
        display_flush();
        stop();
    }

    RP2040* rp2040 = get_rp2040();

    uint8_t brightness = 0xFF;
    nvs_get_u8(handle, "brightness", &brightness);
    rp2040_set_lcd_backlight(rp2040, brightness);

    rp2040_updater(rp2040);  // Handle RP2040 firmware update & bootloader mode

    uint8_t crash_debug;
    if (rp2040_get_crash_state(rp2040, &crash_debug) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RP2040 crash & debug state");
        display_fatal_error(fatal_error_str, "Failed to communicate with", "the RP2040 co-processor", reset_board_str);
        stop();
    }

    bool rp2040_crashed = crash_debug & 0x01;
    bool rp2040_debug   = crash_debug & 0x02;

    if (rp2040_crashed) {
        display_rp2040_crashed_message();
    }

    if (rp2040_debug) {
        display_rp2040_debug_message();
    }

    factory_test();

    /* Apply flashing lock */

    uint8_t prevent_flashing;
    res = nvs_get_u8(handle, "flash_lock", &prevent_flashing);
    if (res != ESP_OK) {
        prevent_flashing = true;
    }

    if (rp2040_set_reset_lock(rp2040, prevent_flashing) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write RP2040 flashing lock state");
        display_fatal_error(fatal_error_str, "Failed to communicate with", "the RP2040 co-processor", reset_board_str);
        stop();
    }

    /* Start FPGA driver */

    if (bsp_ice40_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the ICE40 FPGA");
        display_fatal_error(fatal_error_str, "A hardware failure occured", "while initializing the FPGA", reset_board_str);
        stop();
    }

    ICE40* ice40 = get_ice40();

    /* Start AppFS */
    res = appfs_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "AppFS init failed: %d", res);
        display_fatal_error(fatal_error_str, "Failed to initialize AppFS", "Flash may be corrupted", reset_board_str);
        stop();
    }

    /* Start internal filesystem */
    if (mount_internal_filesystem() != ESP_OK) {
        display_fatal_error(fatal_error_str, "Failed to initialize flash FS", "Flash may be corrupted", reset_board_str);
        stop();
    }

    /* Start SD card filesystem */
    gpio_set_level(GPIO_SD_PWR, 1);  // Enable power to LEDs and SD card

    bool sdcard_mounted = (mount_sdcard_filesystem() == ESP_OK);
    if (sdcard_mounted) {
        ESP_LOGI(TAG, "SD card filesystem mounted");
    }

    /* Start WiFi */
    wifi_init();

    if (!wifi_check_configured()) {
        if (wifi_set_defaults()) {
            const pax_font_t* font = pax_font_saira_regular;
            pax_background(pax_buffer, 0xFFFFFF);
            pax_draw_text(pax_buffer, 0xFF000000, font, 18, 5, 240 - 18, "🅰 continue");
            render_message("Default WiFi settings\nhave been restored!\nPress A to continue...");
            display_flush();
            wait_for_button();
        } else {
            display_fatal_error(fatal_error_str, "Failed to configure WiFi", "Flash may be corrupted", reset_board_str);
            stop();
        }
    }

    res = init_ca_store();
    if (res != ESP_OK) {
        display_fatal_error(fatal_error_str, "Failed to initialize", "TLS certificate storage", reset_board_str);
        stop();
    }

    /* Clear RTC memory */
    rtc_memory_clear();

    /* Check WebUSB mode */

    uint8_t webusb_mode;
    res = rp2040_get_webusb_mode(rp2040, &webusb_mode);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WebUSB mode: %d", res);
        display_fatal_error(fatal_error_str, "Failed to read WebUSB mode", NULL, NULL);
        stop();
    }

    ESP_LOGI(TAG, "WebUSB mode 0x%02X", webusb_mode);

    uint8_t msc_state;
    res = rp2040_get_msc_state(rp2040, &msc_state);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MSC state: %d", res);
        // display_fatal_error(fatal_error_str, "Failed to read MSC state", NULL, NULL);
        // stop();
        msc_state = 0;
    }

    ESP_LOGI(TAG, "MSC state 0x%02X", msc_state);

    if (msc_state > 0) {
        msc_main(rp2040->queue);
    } else if (webusb_mode == 0x00) {  // Normal boot
        if (prevent_flashing) {
            uint8_t attempted;
            if (rp2040_get_reset_attempted(rp2040, &attempted) == ESP_OK) {
                if (attempted) {
                    rp2040_set_reset_attempted(rp2040, false);
                    ESP_LOGE(TAG, "Detected esptool.py style reset while flash lock is active");
                    bool disable_lock = display_rp2040_flash_lock_warning();
                    if (disable_lock) {
                        nvs_set_u8(handle, "flash_lock", 0);
                        nvs_commit(handle);
                        rp2040_set_reset_lock(rp2040, 0);
                    }
                    esp_restart();
                }
            }
        } else {
            ESP_LOGW(TAG, "Flashing using esptool.py is allowed");
        }

        /* Crash check */
        appfs_handle_t crashed_app = appfs_detect_crash();
        if (crashed_app != APPFS_INVALID_FD) {
            const char* app_name = NULL;
            appfsEntryInfo(crashed_app, &app_name, NULL);
            pax_background(pax_buffer, 0xFFFFFF);
            render_header(pax_buffer, 0, 0, pax_buffer->width, 34, 18, 0xFFfa448c, 0xFF491d88, NULL, "App crashed");
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52, "Failed to start app,");
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52 + 20, "check console for more details.");
            if (app_name != NULL) {
                char buffer[64];
                buffer[sizeof(buffer) - 1] = '\0';
                snprintf(buffer, sizeof(buffer) - 1, "App: %s", app_name);
                pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, 52 + 40, buffer);
            }
            pax_draw_text(pax_buffer, 0xFF491d88, pax_font_saira_regular, 18, 5, pax_buffer->height - 18, "🅰 continue");
            display_flush();
            wait_for_button();
        }

        /* Rick that roll */
        xTaskCreate(audio_player_task, "audio_player_task", 2048, NULL, 12, NULL);

        my_uart_init();

        /* Launcher menu */
        while (true) {
            menu_start(rp2040->queue, app_description->version);
        }
    } else if (webusb_mode == 0x01) {
        webusb_main(rp2040->queue);
    } else if (webusb_mode == 0x02) {
        display_boot_screen("FPGA download mode");
        while (true) {
            fpga_download(rp2040->queue, ice40);
        }
    } else if (webusb_mode == 0x03) {
        webusb_new_main(rp2040->queue);
    } else {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Invalid mode 0x%02X", webusb_mode);
        display_boot_screen(buffer);
    }

    nvs_close(handle);
}
