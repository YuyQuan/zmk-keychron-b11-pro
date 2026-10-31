/*
 * KeyPeek labels for ZMK bindings that have no VIA keycode of their own.
 *
 * map_keycode.c turns each entry into QK_USER_<position>, so the order matters: append only. Names are
 * matched on the behavior's devicetree label (the node name for built-in behaviors).
 *
 * KP_NAME(behavior label, param1 or KP_ANY, name shown on the key)
 */
#ifndef KEYPEEK_NAMES_H
#define KEYPEEK_NAMES_H

#include <dt-bindings/zmk/bt.h>
#include <dt-bindings/zmk/outputs.h>

#include <dt-bindings/zmk/dynamic_macro.h>

#define KP_ANY (-1)
/* &dm has two params (command, slot); map_keycode.c matches them as one value. */
#define DM_KEY(cmd, slot) ((cmd) | ((slot) << 8))

#define KEYPEEK_NAMES(KP_NAME)                      \
    KP_NAME("bluetooth", BT_CLR_CMD, "BT Clr")      \
    KP_NAME("outputs", OUT_USB, "Out USB")          \
    KP_NAME("outputs", OUT_BLE, "Out BT")           \
    KP_NAME("outputs", OUT_PPT, "Out 2.4G")         \
    KP_NAME("outputs", OUT_CHG, "Chg")              \
    KP_NAME("outputs", OUT_CHD, "Chg done")         \
    KP_NAME("DYN_MACRO", DM_KEY(DM_REC_CMD, 0), "DM Rec1") \
    KP_NAME("DYN_MACRO", DM_KEY(DM_REC_CMD, 1), "DM Rec2") \
    KP_NAME("DYN_MACRO", DM_KEY(DM_STP_CMD, 0), "DM Stop") \
    KP_NAME("DYN_MACRO", DM_KEY(DM_PLY_CMD, 0), "DM Play1") \
    KP_NAME("DYN_MACRO", DM_KEY(DM_PLY_CMD, 1), "DM Play2")

#endif
