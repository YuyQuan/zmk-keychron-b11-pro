/*
 * KeyPeek (https://github.com/srwi/keypeek) notifications over the launcher's
 * VIA raw HID channel: a port of qmk-modules' keypeek_layer_notify.
 *
 * KeyPeek pings 0xC0 every ~1 s while it is open; only then do layer changes
 * (0xFF packets) and key presses (0xF1 packets) go out, so an idle board
 * sends nothing. Packets are 32 bytes, USB or 2.4G (raw_hid_send drops BLE).
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#define RAW_EPSIZE 32 // launcher.h's RAW_EPSIZE; launcher.h is not included here, it defines static data
void raw_hid_send(uint8_t *data, uint8_t length);

#define KEYPEEK_LAYER_PACKET 0xFF
#define KEYPEEK_KEY_PACKET 0xF1
#define KEYPEEK_SUBSCRIBE 0xC0
#define KEYPEEK_SUBSCRIBE_INACTIVE 0xA0
#define KEYPEEK_TIMEOUT_MS 3000

static bool subscribed;
static int64_t last_ping;

static bool active(void) { return subscribed && k_uptime_get() - last_ping < KEYPEEK_TIMEOUT_MS; }

static void send_layer_state(void) {
    uint8_t d[RAW_EPSIZE] = {KEYPEEK_LAYER_PACKET, sizeof(uint32_t), 1}; // default layer = layer 0
    sys_put_le32(zmk_keymap_layer_state(), &d[6]);
    raw_hid_send(d, RAW_EPSIZE);
}

// Overrides launcher.c's weak launcher_command_kb() (called first in raw_hid_receive); true = consumed, no reply.
bool launcher_command_kb(uint8_t *data, uint8_t length) {
    if (length < 1 || data[0] != KEYPEEK_SUBSCRIBE) {
        return false;
    }
    bool was_active = active();
    subscribed = length < 2 || data[1] != KEYPEEK_SUBSCRIBE_INACTIVE;
    last_ping = k_uptime_get();
    if (subscribed && !was_active) {
        send_layer_state();
    }
    return true;
}

// Called from kscan.c with matrix coordinates, before the position transform.
void keypeek_key_event(uint32_t row, uint32_t col, bool pressed) {
    if (row >= 8 || !active()) { // row 8 is the mac/win, BT and 2.4G slider switches
        return;
    }
    uint8_t d[RAW_EPSIZE] = {KEYPEEK_KEY_PACKET, row, col, pressed};
    raw_hid_send(d, RAW_EPSIZE);
}

static int layer_listener(const zmk_event_t *eh) {
    if (active()) {
        send_layer_state();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(keypeek_layer, layer_listener);
ZMK_SUBSCRIPTION(keypeek_layer, zmk_layer_state_changed);
