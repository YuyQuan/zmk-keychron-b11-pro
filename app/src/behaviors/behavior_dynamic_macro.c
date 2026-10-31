/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Record / play back key events at runtime (QMK dynamic macros). Ported from
 * zmkfirmware/zmk PR #2678 (via the B1 Pro fork, keychron_bpro 56b51548).
 * Usage: &dm DM_REC 0, &dm DM_STP 0, &dm DM_PLY 0
 */

#define DT_DRV_COMPAT zmk_behavior_dynamic_macro

#include <string.h>
#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <dt-bindings/zmk/dynamic_macro.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

enum dynamic_macro_state {
    DYNAMIC_MACRO_STATE_STOPPED = 0,
    DYNAMIC_MACRO_STATE_RECORDING,
    DYNAMIC_MACRO_STATE_PLAYING,
};

struct dynamic_macro_slot {
    uint8_t state;
    uint32_t current_event;
    uint32_t event_count;
    struct zmk_keycode_state_changed events[CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_EVENTS];
};

struct behavior_dynamic_macro_data {
    struct dynamic_macro_slot slots[CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_SLOTS];
};

static struct behavior_dynamic_macro_data dynamic_macro_data;

static void dynamic_macro_work_handler(struct k_work *work);
K_WORK_DEFINE(dynamic_macro_work, dynamic_macro_work_handler);

static void dynamic_macro_timer_submit(struct k_timer *timer) { k_work_submit(&dynamic_macro_work); }
K_TIMER_DEFINE(dynamic_macro_work_timer, dynamic_macro_timer_submit, NULL);

static void dynamic_macro_work_handler(struct k_work *work) {
    bool handled = false;

    for (int i = 0; i < CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_SLOTS; i++) {
        struct dynamic_macro_slot *slot = &dynamic_macro_data.slots[i];
        if (slot->state != DYNAMIC_MACRO_STATE_PLAYING) {
            continue;
        }

        struct zmk_keycode_state_changed ev = slot->events[slot->current_event++];
        ev.timestamp = k_uptime_get();
        raise_zmk_keycode_state_changed(ev);

        if (slot->current_event >= slot->event_count) {
            slot->current_event = 0;
            slot->state = DYNAMIC_MACRO_STATE_STOPPED;
        }
        handled = true;
    }

    if (!handled) {
        k_timer_stop(&dynamic_macro_work_timer);
    }
}

static int on_dynamic_macro_binding_pressed(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    if (binding->param2 >= CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_SLOTS) {
        LOG_ERR("Dynamic macro slot %d out of range", binding->param2);
        return -EINVAL;
    }

    struct dynamic_macro_slot *slot = &dynamic_macro_data.slots[binding->param2];

    switch (binding->param1) {
    case DM_REC:
        k_timer_stop(&dynamic_macro_work_timer);
        slot->state = DYNAMIC_MACRO_STATE_RECORDING;
        slot->current_event = 0;
        slot->event_count = 0;
        break;
    case DM_STP:
        /* Like QMK's DM_RSTP: stops whichever slot is recording, so one key serves all slots. */
        for (int i = 0; i < CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_SLOTS; i++) {
            if (dynamic_macro_data.slots[i].state == DYNAMIC_MACRO_STATE_RECORDING) {
                dynamic_macro_data.slots[i].state = DYNAMIC_MACRO_STATE_STOPPED;
            }
        }
        break;
    case DM_PLY:
        if (slot->state != DYNAMIC_MACRO_STATE_STOPPED || slot->event_count == 0) {
            break;
        }
        slot->state = DYNAMIC_MACRO_STATE_PLAYING;
        slot->current_event = 0;
        k_timer_start(&dynamic_macro_work_timer, K_NO_WAIT,
                      K_MSEC(CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_TAP_DELAY));
        break;
    default:
        LOG_ERR("Unknown DM command: %d", binding->param1);
        return -ENOTSUP;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_dynamic_macro_binding_released(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_dynamic_macro_driver_api = {
    .binding_pressed = on_dynamic_macro_binding_pressed,
    .binding_released = on_dynamic_macro_binding_released,
};

static int dynamic_macro_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_SLOTS; i++) {
        struct dynamic_macro_slot *slot = &dynamic_macro_data.slots[i];
        if (slot->state != DYNAMIC_MACRO_STATE_RECORDING) {
            continue;
        }
        if (slot->event_count >= CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_EVENTS) {
            LOG_WRN("Dynamic macro %d full, dropping event", i);
            continue;
        }
        slot->events[slot->event_count++] = *ev;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_dynamic_macro, dynamic_macro_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_dynamic_macro, zmk_keycode_state_changed);

static int behavior_dynamic_macro_init(const struct device *dev) { return 0; }

BEHAVIOR_DT_INST_DEFINE(0, behavior_dynamic_macro_init, NULL, &dynamic_macro_data, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_dynamic_macro_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
