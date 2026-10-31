/*
 * The launcher's raw HID channel (usage page 0xFF60, 32-byte reports, no report id) over BLE:
 * a second HID service next to hog_svc, so the host (BlueZ) makes it its own hidraw node, as the
 * USB interface is. That is what KeyPeek and VIA tools open. A report id on the keyboard's own
 * map would break unnumbered 32-byte writes, and hidapi reads the usage page of the first
 * collection only.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

#include "launcher.h"

struct bt_conn *destination_connection(void); // hog.c
extern struct k_work_q hog_work_q;            // hog.c

#define HIDS_INPUT 0x01
#define HIDS_OUTPUT 0x02
#define PKT RAW_EPSIZE

struct hids_report {
    uint8_t id;
    uint8_t type;
} __packed;

static const uint8_t hids_info[] = {0x00, 0x00, 0x00, 0x03}; // HID 0.0, no country, remote wake + normally connectable
static const struct hids_report input_ref = {.id = 0, .type = HIDS_INPUT};
static const struct hids_report output_ref = {.id = 0, .type = HIDS_OUTPUT};
static bool notify_on;
static uint8_t ctrl_point;

K_MSGQ_DEFINE(rx_q, PKT, 8, 4);
static void rx_worker(struct k_work *work);
static K_WORK_DEFINE(rx_work, rx_worker);
K_MSGQ_DEFINE(tx_q, PKT, 16, 4);

// Report map and references stay readable before encryption: see the note in hog.c.
static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                         uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hids_info, sizeof(hids_info));
}

static ssize_t read_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                        uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, zmk_hid_launcher_report_desc,
                             sizeof(zmk_hid_launcher_report_desc));
}

static ssize_t read_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                        uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static ssize_t read_input(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                          uint16_t len, uint16_t offset) {
    static const uint8_t zeros[PKT];
    return bt_gatt_attr_read(conn, attr, buf, len, offset, zeros, sizeof(zeros));
}

static ssize_t write_output(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    // BlueZ forwards hidraw's leading report id 0 for an unnumbered report, so a host write is 33 bytes
    if (len == PKT + 1 && ((const uint8_t *)buf)[0] == 0) {
        buf = (const uint8_t *)buf + 1;
        len = PKT;
    }
    if (len != PKT) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    k_msgq_put(&rx_q, buf, K_NO_WAIT); // full: drop, the host retries
    k_work_submit(&rx_work);
    return len;
}

static ssize_t write_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                          uint16_t len, uint16_t offset, uint8_t flags) {
    return len;
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    notify_on = value == BT_GATT_CCC_NOTIFY;
}

/* Attribute layout, index: 0 service, 1-2 info, 3-4 report map, 5-6 input report, 7 its CCC,
 * 8 its reference, 9-10 output report, 11 its reference, 12-13 control point. */
BT_GATT_SERVICE_DEFINE(
    launcher_hid_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_info, NULL,
                           NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_map,
                           NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_input, NULL, NULL),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_ref, NULL,
                       (void *)&input_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, read_input,
                           write_output, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_ref, NULL,
                       (void *)&output_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_ctrl, &ctrl_point));

static void rx_worker(struct k_work *work) {
    uint8_t pkt[PKT];
    while (k_msgq_get(&rx_q, pkt, K_NO_WAIT) == 0) {
        launcher_set_path(LAUNCHER_PATH_BLE);
        raw_hid_receive(pkt, PKT);
    }
}

static void tx_worker(struct k_work *work) {
    uint8_t pkt[PKT];
    while (notify_on && k_msgq_get(&tx_q, pkt, K_NO_WAIT) == 0) {
        struct bt_conn *conn = destination_connection();
        if (conn == NULL) {
            return;
        }
        struct bt_gatt_notify_params params = {
            .attr = &launcher_hid_svc.attrs[5], // input report characteristic
            .data = pkt,
            .len = PKT,
        };
        bt_gatt_notify_cb(conn, &params); // best effort: KeyPeek packets can drop, replies are retried by the host
        bt_conn_unref(conn);
    }
}
static K_WORK_DEFINE(tx_work, tx_worker);

void launcher_ble_send(const uint8_t *data) {
    if (!notify_on) {
        return;
    }
    while (k_msgq_put(&tx_q, data, K_NO_WAIT) == -ENOMSG) { // full: drop the oldest
        uint8_t old[PKT];
        k_msgq_get(&tx_q, old, K_NO_WAIT);
    }
    k_work_submit_to_queue(&hog_work_q, &tx_work);
}
