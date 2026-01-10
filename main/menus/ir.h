#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdbool.h>

typedef enum {
    MENU_ACTION_IR,
    MENU_ACTION_IR_RENZE,
    MENU_ACTION_IR_SAMSUNG
} menu_ir_action_t;

void menu_ir(xQueueHandle button_queue, menu_ir_action_t menu_ir_action);
