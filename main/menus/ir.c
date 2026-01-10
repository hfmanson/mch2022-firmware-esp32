#include "ir.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>

#include "appfs.h"
#include "hardware.h"
#include "menu.h"
#include "pax_gfx.h"
#include "rp2040.h"

#define IR_ADDR_DECO           0xD5D5
#define IR_CMD_BRIGHTNESS_UP   0x01
#define IR_CMD_BRIGHTNESS_DOWN 0x27
#define IR_CMD_OFF             0x56
#define IR_CMD_ON              0x51
#define IR_CMD_RED             0x08
#define IR_CMD_GREEN           0x18
#define IR_CMD_BLUE            0x60
#define IR_CMD_WHITE           0x45
#define IR_CMD_ORANGE          0x31
#define IR_CMD_LIGHT_GREEN     0x34
#define IR_CMD_LIGHT_BLUE      0x03
#define IR_CMD_TIMER_1HOUR     0x15
#define IR_CMD_YELLOW          0x32
#define IR_CMD_ALTERNATE_GREEN 0x33
#define IR_CMD_PURPLE          0x22
#define IR_CMD_TIMER_2HOURS    0x24
#define IR_CMD_SPEED_INCREASE  0x41
#define IR_CMD_MODE            0x30
#define IR_CMD_SPEED_DECREASE  0x17
#define IR_CMD_NIGHT           0x04
#define IR_CMD_MUSIC1          0x16
#define IR_CMD_MUSIC2          0x25
#define IR_CMD_MUSIC3          0x46
#define IR_CMD_MUSIC4          0x42

#define IR_ADDR_BADGE                0xEF00
#define IR_CMD_BADGE_BRIGHTNESS_UP   0x00
#define IR_CMD_BADGE_BRIGHTNESS_DOWN 0x01
#define IR_CMD_BADGE_OFF             0x02
#define IR_CMD_BADGE_ON              0x03
#define IR_CMD_BADGE_RED             0x04
#define IR_CMD_BADGE_GREEN           0x05
#define IR_CMD_BADGE_BLUE            0x06
#define IR_CMD_BADGE_WHITE           0x07
#define IR_CMD_BADGE_ORANGE          0x08
#define IR_CMD_BADGE_LIGHT_GREEN     0x09
#define IR_CMD_BADGE_LIGHT_BLUE      0x0A
#define IR_CMD_BADGE_FLASH           0x0B
#define IR_CMD_BADGE_LIGHT_ORANGE    0x0C
#define IR_CMD_BADGE_TEAL            0x0D
#define IR_CMD_BADGE_DARK_BLUE       0x0E
#define IR_CMD_BADGE_STROBE          0x17  // Firmware bug in the ledstrip has this button swapped
#define IR_CMD_BADGE_LIGHTER_ORANGE  0x10
#define IR_CMD_BADGE_DARK_GREEN      0x11
#define IR_CMD_BADGE_PURPLE          0x12
#define IR_CMD_BADGE_FADE            0x13
#define IR_CMD_BADGE_YELLOW          0x14
#define IR_CMD_BADGE_DARKER_GREEN    0x15
#define IR_CMD_BADGE_PINK            0x16
#define IR_CMD_BADGE_SMOOTH          0x0F  // Firmware bug in the ledstrip has this button swapped

#define IR_ADDR_SAMSUNG        0x0707
#define IR_CMD_INPUT_SOURCE    1
#define IR_CMD_POWER           2
#define IR_CMD_1               4
#define IR_CMD_2               5
#define IR_CMD_3               6
#define IR_CMD_VOLUME_UP       7
#define IR_CMD_4               8
#define IR_CMD_5               9
#define IR_CMD_6              10
#define IR_CMD_VOLUME_DOWN    11
#define IR_CMD_7              12
#define IR_CMD_8              13
#define IR_CMD_9              14
#define IR_CMD_MUTE           15
#define IR_CMD_CHANNEL_DOWN   16
#define IR_CMD_0              17
#define IR_CMD_CHANNEL_UP     18
#define IR_CMD_LAST           19
#define IR_CMD_MENU           26
#define IR_CMD_INFO           31
#define IR_CMD_AD_SUBT        37
#define IR_CMD_EXIT           45
#define IR_CMD_E_MANUAL       63
#define IR_CMD_TOOLS          75
#define IR_CMD_GUIDE          79
#define IR_CMD_RETURN         88
#define IR_CMD_CURSOR_UP      96
#define IR_CMD_CURSOR_DOWN    97
#define IR_CMD_CURSOR_RIGHT   98
#define IR_CMD_CURSOR_LEFT   101
#define IR_CMD_ENTER         104
#define IR_CMD_CH LIST       107
#define IR_CMD_SMART_HUB     121
#define IR_CMD_3D            159
#define IR_CMD_HDMI2         190
#define IR_CMD_HDMI3         194
#define IR_CMD_HDMI4         197
#define IR_CMD_HDMI1         233

static void send_ir(uint16_t address, uint8_t command) {
    RP2040* rp2040 = get_rp2040();
    rp2040_ir_send(rp2040, address, command);
}

static void send_ir_repeated(uint16_t address, uint8_t command) {
    for (uint8_t i = 0; i < 4; i++) {
        send_ir(address, command);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void menu_ir(xQueueHandle button_queue, menu_ir_action_t menu_ir_action) {
    pax_buf_t*        pax_buffer = get_pax_buffer();
    const pax_font_t* font       = pax_font_saira_regular;
    menu_t*           menu       = menu_alloc("Infrared remote", 34, 16);
    menu->fgColor                = 0xFF000000;
    menu->bgColor                = 0xFFFFFFFF;
    menu->bgTextColor            = 0xFF000000;
    menu->selectedItemColor      = 0xFFfec859;
    menu->borderColor            = 0xFF491d88;
    menu->titleColor             = 0xFFfec859;
    menu->titleBgColor           = 0xFF491d88;
    menu->scrollbarBgColor       = 0xFFCCCCCC;
    menu->scrollbarFgColor       = 0xFF555555;

    menu->grid_entry_count_x = 6;
    menu->grid_entry_count_y = 4;
    uint16_t address = IR_ADDR_DECO;

    switch (menu_ir_action) {
    case MENU_ACTION_IR:
        address = IR_ADDR_DECO;
        menu_insert_item(menu, "OFF", NULL, (void*) IR_CMD_OFF, -1);
        menu_insert_item(menu, "ON", NULL, (void*) IR_CMD_ON, -1);
        menu_insert_item(menu, "B+", NULL, (void*) IR_CMD_BRIGHTNESS_UP, -1);
        menu_insert_item(menu, "B-", NULL, (void*) IR_CMD_BRIGHTNESS_DOWN, -1);
        menu_insert_item(menu, "S+", NULL, (void*) IR_CMD_SPEED_INCREASE, -1);
        menu_insert_item(menu, "S-", NULL, (void*) IR_CMD_SPEED_DECREASE, -1);

        menu_insert_item(menu, "Red", NULL, (void*) IR_CMD_RED, -1);
        menu_insert_item(menu, "Green", NULL, (void*) IR_CMD_GREEN, -1);
        menu_insert_item(menu, "Blue", NULL, (void*) IR_CMD_BLUE, -1);
        menu_insert_item(menu, "White", NULL, (void*) IR_CMD_WHITE, -1);
        menu_insert_item(menu, "1H", NULL, (void*) IR_CMD_TIMER_1HOUR, -1);
        menu_insert_item(menu, "2H", NULL, (void*) IR_CMD_TIMER_2HOURS, -1);

        menu_insert_item(menu, "Orange", NULL, (void*) IR_CMD_ORANGE, -1);
        menu_insert_item(menu, "Light\ngreen", NULL, (void*) IR_CMD_LIGHT_BLUE, -1);
        menu_insert_item(menu, "Light\nblue", NULL, (void*) IR_CMD_LIGHT_GREEN, -1);
        menu_insert_item(menu, "Yellow", NULL, (void*) IR_CMD_YELLOW, -1);
        menu_insert_item(menu, "Dark\ngreen", NULL, (void*) IR_CMD_ALTERNATE_GREEN, -1);
        menu_insert_item(menu, "Purple", NULL, (void*) IR_CMD_PURPLE, -1);

        menu_insert_item(menu, "Music\n1", NULL, (void*) IR_CMD_MUSIC1, -1);
        menu_insert_item(menu, "Music\n2", NULL, (void*) IR_CMD_MUSIC2, -1);
        menu_insert_item(menu, "Music\n3", NULL, (void*) IR_CMD_MUSIC3, -1);
        menu_insert_item(menu, "Music\n4", NULL, (void*) IR_CMD_MUSIC4, -1);
        menu_insert_item(menu, "Mode", NULL, (void*) IR_CMD_MODE, -1);
        menu_insert_item(menu, "Night", NULL, (void*) IR_CMD_NIGHT, -1);
        break;
    case MENU_ACTION_IR_RENZE:
        address = IR_ADDR_BADGE;
        menu_insert_item(menu, "B+", NULL, (void*) IR_CMD_BADGE_BRIGHTNESS_UP, -1);
        menu_insert_item(menu, "B-", NULL, (void*) IR_CMD_BADGE_BRIGHTNESS_DOWN, -1);
        menu_insert_item(menu, "Off", NULL, (void*) IR_CMD_BADGE_OFF, -1);
        menu_insert_item(menu, "On", NULL, (void*) IR_CMD_BADGE_ON, -1);
        menu_insert_item(menu, "Flash", NULL, (void*) IR_CMD_BADGE_FLASH, -1);
        menu_insert_item(menu, "Strobe", NULL, (void*) IR_CMD_BADGE_STROBE, -1);

        menu_insert_item(menu, "Red", NULL, (void*) IR_CMD_BADGE_RED, -1);
        menu_insert_item(menu, "Green", NULL, (void*) IR_CMD_BADGE_GREEN, -1);
        menu_insert_item(menu, "Blue", NULL, (void*) IR_CMD_BADGE_BLUE, -1);
        menu_insert_item(menu, "White", NULL, (void*) IR_CMD_BADGE_WHITE, -1);
        menu_insert_item(menu, "Fade", NULL, (void*) IR_CMD_BADGE_FADE, -1);
        menu_insert_item(menu, "Smooth", NULL, (void*) IR_CMD_BADGE_SMOOTH, -1);

        menu_insert_item(menu, "Orange", NULL, (void*) IR_CMD_BADGE_ORANGE, -1);
        menu_insert_item(menu, "Light\ngreen", NULL, (void*) IR_CMD_BADGE_LIGHT_GREEN, -1);
        menu_insert_item(menu, "Light\nblue", NULL, (void*) IR_CMD_BADGE_LIGHT_BLUE, -1);
        menu_insert_item(menu, "Light\norange", NULL, (void*) IR_CMD_BADGE_LIGHT_ORANGE, -1);
        menu_insert_item(menu, "Teal", NULL, (void*) IR_CMD_BADGE_TEAL, -1);
        menu_insert_item(menu, "Dark\nblue", NULL, (void*) IR_CMD_BADGE_DARK_BLUE, -1);

        menu_insert_item(menu, "Lighter\norange", NULL, (void*) IR_CMD_BADGE_LIGHTER_ORANGE, -1);
        menu_insert_item(menu, "Dark\ngreen", NULL, (void*) IR_CMD_BADGE_DARK_GREEN, -1);
        menu_insert_item(menu, "Purple", NULL, (void*) IR_CMD_BADGE_PURPLE, -1);
        menu_insert_item(menu, "Yellow", NULL, (void*) IR_CMD_BADGE_YELLOW, -1);
        menu_insert_item(menu, "Darker\ngreen", NULL, (void*) IR_CMD_BADGE_DARKER_GREEN, -1);
        menu_insert_item(menu, "Pink", NULL, (void*) IR_CMD_BADGE_PINK, -1);
        break;
    case MENU_ACTION_IR_SAMSUNG:
        address = IR_ADDR_SAMSUNG;
        menu_insert_item(menu, "On", NULL, (void*) IR_CMD_POWER, -1);
        menu_insert_item(menu, "Up", NULL, (void*) IR_CMD_CURSOR_UP, -1);
        menu_insert_item(menu, "Smart", NULL, (void*) IR_CMD_SMART_HUB, -1);
        menu_insert_item(menu, "Vol +", NULL, (void*) IR_CMD_VOLUME_UP, -1);
        menu_insert_item(menu, "Ch +", NULL, (void*) IR_CMD_CHANNEL_UP, -1);
        menu_insert_item(menu, "Mute", NULL, (void*) IR_CMD_MUTE, -1);

        menu_insert_item(menu, "Left", NULL, (void*) IR_CMD_CURSOR_LEFT, -1);
        menu_insert_item(menu, "Enter", NULL, (void*) IR_CMD_ENTER, -1);
        menu_insert_item(menu, "Right", NULL, (void*) IR_CMD_CURSOR_RIGHT, -1);
        menu_insert_item(menu, "Vol -", NULL, (void*) IR_CMD_VOLUME_DOWN, -1);
        menu_insert_item(menu, "Ch -", NULL, (void*) IR_CMD_CHANNEL_DOWN, -1);
        menu_insert_item(menu, "Exit", NULL, (void*) IR_CMD_EXIT, -1);

        menu_insert_item(menu, "Info", NULL, (void*) IR_CMD_INFO, -1);
        menu_insert_item(menu, "Down", NULL, (void*) IR_CMD_CURSOR_DOWN, -1);
        menu_insert_item(menu, "Menu", NULL, (void*) IR_CMD_MENU, -1);
        menu_insert_item(menu, "Tools", NULL, (void*) IR_CMD_TOOLS, -1);
        menu_insert_item(menu, "Input\nSource", NULL, (void*) IR_CMD_INPUT_SOURCE, -1);
        menu_insert_item(menu, "Return", NULL, (void*) IR_CMD_RETURN, -1);
        break;
    }

    pax_noclip(pax_buffer);

    bool render = true;
    bool exit   = false;
    while (!exit) {
        rp2040_input_message_t buttonMessage = {0};
        if (xQueueReceive(button_queue, &buttonMessage, portMAX_DELAY) == pdTRUE) {
            if (buttonMessage.state) {
                switch (buttonMessage.input) {
                    case RP2040_INPUT_JOYSTICK_PRESS:
                    case RP2040_INPUT_BUTTON_ACCEPT:
                        {
                            uint32_t command = (uint32_t) menu_get_callback_args(menu, menu_get_position(menu));
                            if ((command == IR_CMD_SPEED_DECREASE) || (command == IR_CMD_SPEED_INCREASE) || (command == IR_CMD_MODE) ||
                                (command == IR_CMD_BRIGHTNESS_UP) || (command == IR_CMD_BRIGHTNESS_DOWN) ||
                                (command == IR_CMD_VOLUME_UP || command == IR_CMD_VOLUME_DOWN) ||
                                (command == IR_CMD_CHANNEL_UP || command == IR_CMD_CHANNEL_DOWN)) {
                                send_ir(address, command);
                            } else {
                                send_ir_repeated(address, command);
                            }
                            break;
                        }
                    case RP2040_INPUT_BUTTON_BACK:
                    case RP2040_INPUT_BUTTON_HOME:
                        exit = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_DOWN:
                        menu_navigate_next_row(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_UP:
                        menu_navigate_previous_row(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_LEFT:
                        menu_navigate_previous(menu);
                        render = true;
                        break;
                    case RP2040_INPUT_JOYSTICK_RIGHT:
                        menu_navigate_next(menu);
                        render = true;
                        break;
                    default:
                        break;
                }
            }
        }

        if (render) {
            pax_background(pax_buffer, 0xFFFFFF);
            menu_render_grid(pax_buffer, menu, 0, 0, 320, 220);
            pax_draw_text(pax_buffer, 0xFF491d88, font, 18, 5, 240 - 18, "🅰 send 🅱 back");
            display_flush();
            render = false;
        }
    }

    menu_free(menu);
}
