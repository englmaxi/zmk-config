/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>


#include "battery_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_status_state {
    uint8_t level[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
} battery_state;

struct battery_widget_object {
    lv_obj_t *battery_image_canvas;
    uint8_t battery_image_buffer[5 * 8];
    lv_obj_t *battery_label;
};

struct battery_widget_object battery_widget_objects[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];

static void draw_battery(lv_obj_t *canvas, uint8_t level) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    
    lv_draw_rect_dsc_t rect_fill_dsc;
    lv_draw_rect_dsc_init(&rect_fill_dsc);
    rect_fill_dsc.bg_color = lv_color_white();

    lv_canvas_set_px(canvas, 0, 0, lv_color_white());
    lv_canvas_set_px(canvas, 4, 0, lv_color_white());

    if (level > 90) {
        // full
    } else if (level > 70) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 1, &rect_fill_dsc);
    } else if (level > 50) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 2, &rect_fill_dsc);
    } else if (level > 30) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 3, &rect_fill_dsc);
    } else if (level > 10) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 4, &rect_fill_dsc);
    } else if (level > 0) {
        lv_canvas_draw_rect(canvas, 1, 2, 3, 5, &rect_fill_dsc);
    } else {
        lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
    }
}

static void set_battery_symbol(lv_obj_t *widged, struct battery_status_state state) {

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        uint8_t level = state.level[i];
        lv_obj_t *label = battery_widget_objects[i].battery_label;
        lv_obj_t *symbol = battery_widget_objects[i].battery_image_canvas;
        
        char text[5] = {};

        if (level > 0) {
            snprintf(text, sizeof(text), "%3u%%", level);
        }

        lv_label_set_text(label, text);

        draw_battery(symbol, level);
    }
}

void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_symbol(widget->obj, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    battery_state.level[ev->source] = ev->state_of_charge;
    return battery_state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_peripheral_battery_state_changed);

int zmk_widget_peripheral_battery_status_init(struct zmk_widget_peripheral_battery_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);

    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        battery_widget_objects[i].battery_image_canvas = lv_canvas_create(widget->obj);
        battery_widget_objects[i].battery_label = lv_label_create(widget->obj);

        lv_canvas_set_buffer(battery_widget_objects[i].battery_image_canvas, battery_widget_objects[i].battery_image_buffer, 5, 8, LV_IMG_CF_TRUE_COLOR);

        lv_obj_align(battery_widget_objects[i].battery_image_canvas, LV_ALIGN_TOP_RIGHT, 0, i * 10);
        lv_obj_align(battery_widget_objects[i].battery_label, LV_ALIGN_TOP_RIGHT, -7, i * 10);
    }

    sys_slist_append(&widgets, &widget->node);

    widget_battery_status_init();
    return 0;
}

lv_obj_t *zmk_widget_peripheral_battery_status_obj(struct zmk_widget_peripheral_battery_status *widget) {
    return widget->obj;
}