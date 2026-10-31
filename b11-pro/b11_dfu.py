#!/usr/bin/env python3
"""Keychron B11 Pro in-app DFU over HID interface 2 (3434:07b0, usage page 0x8c).

  b11_dfu.py query                 read-only: 0x60 model, 0x61 dfu version, 0x6E patch versions, 0xF2 ids
  b11_dfu.py send FILE             SET_SC_METHOD, START, SEND_BIN..., VERIFY_CRC32. Stops there: image sits in
                                   the OTA temp slot, running app untouched
  b11_dfu.py send FILE --switch    same, then IMAGE_SWITCH (0x66): chip resets and the boot code installs it

Protocol: b11-pro/dfu-protocol.md. Linux only (hidraw). Needs read/write access to the hidraw node (udev rule in README.md).
"""
import glob, os, select, struct, sys, time, zlib

VID, PID = "3434", "07B0"
REPORT_ID = 0xB2


def find_node():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        dev = os.path.realpath(d + "/device")
        if f":{VID}:{PID}." not in dev.upper():
            continue
        try:
            ifn = int(open(dev + "/../bInterfaceNumber").read().strip(), 16)
        except OSError:
            continue
        if ifn == 2:
            return "/dev/" + os.path.basename(d)
    sys.exit("interface 2 node of 3434:07b0 not found")


def frame(hdr2, sn, op, params=b""):
    body = bytes([op]) + params
    ln = len(body) + 2
    f = bytes([0xAA, hdr2, ln, (~ln) & 0xFF, sn]) + body + struct.pack("<H", sum(body) & 0xFFFF)
    return f.ljust(32, b"\0")


class Dev:
    def __init__(self):
        self.path = find_node()
        self.fd = os.open(self.path, os.O_RDWR | os.O_NONBLOCK)

    def drain(self):
        while select.select([self.fd], [], [], 0)[0]:
            os.read(self.fd, 64)

    def send(self, f):
        os.write(self.fd, bytes([REPORT_ID]) + f)

    def recv(self, timeout, single=False):
        """Reports arriving until `timeout` s of silence (or the first one if single); report id stripped."""
        out = []
        while select.select([self.fd], [], [], timeout)[0]:
            r = os.read(self.fd, 64)
            out.append(r[1:] if r[:1] == b"\xb1" else r)
            if single:
                break
            timeout = 0.15
        return out

    def cmd(self, hdr2, sn, op, params=b"", timeout=1.5, single=False):
        self.drain()
        self.send(frame(hdr2, sn, op, params))
        return self.recv(timeout, single)


def crc_raw(data):                 # tdfu.c CRC32: zlib crc32 without the final xor
    return zlib.crc32(data) ^ 0xFFFFFFFF


def show(label, reps):
    print(f"{label}: {len(reps)} report(s)")
    for r in reps:
        print("   ", r.hex())


def query():
    d = Dev()
    print("node", d.path)
    show("0x60 model info", d.cmd(0x55, 2, 0x60))
    show("0x61 dfu version", d.cmd(0x55, 2, 0x61))
    show("0x6E patch versions", d.cmd(0x56, 1, 0x6E))
    show("0xF2 private data", d.cmd(0x55, 2, 0xF2))


def status_ok(reps):
    # reply without report id: AA 55 len ~len dev_sn rsp_cmd host_sn ack_cmd status ...
    return bool(reps) and reps[0][:2] == b"\xaa\x55" and reps[0][8] == 0


def send(path, do_switch):
    img = open(path, "rb").read()
    d = Dev()
    print("node", d.path, "image", len(img), "bytes")
    r = d.cmd(0x55, 2, 0x62, b"\x00")
    print("SET_SC_METHOD", r[0].hex() if r else "no reply")
    r = d.cmd(0x56, 3, 0x63, b"\x00")           # dfu_version_h = 0 (query showed it)
    print("START", r[0].hex() if r else "no reply")
    if not status_ok(r):
        sys.exit("START not acknowledged; nothing was written")
    t0, sn, n = time.time(), 4, 0
    frames = [img[i:i + 16] for i in range(0, len(img), 16)]
    for i, chunk in enumerate(frames):
        for attempt in range(5):
            r = d.cmd(0x56, sn, 0x64, chunk, timeout=0.2, single=True)
            if r and r[0][:2] == b"\xaa\x55" and r[0][8] == 0:
                break
            if r and r[0][8] != 0:
                sys.exit(f"frame {i}: device status {r[0][8]}: {r[0].hex()}")
        else:
            sys.exit(f"frame {i}: no ack after 5 tries; image in temp slot is partial, app untouched")
        sn = 1 if sn == 255 else sn + 1
        if i % 1000 == 0:
            print(f"  {i}/{len(frames)} frames, {time.time() - t0:.0f}s", flush=True)
    crc = struct.pack("<I", crc_raw(img))
    last = r[0]
    print("device running crc after last frame:", last[9:13].hex(), "host crc:", crc.hex())
    r = d.cmd(0x55, 1, 0x65, crc + crc, timeout=3)
    print("VERIFY_CRC32", r[0].hex() if r else "no reply")
    # payload byte 0: 0 ok / 1 crc bad / 2 sha256 or customer_name bad
    if not (r and r[0][8] != 0xFF and r[0][9] == 0):
        sys.exit("VERIFY failed; not switching")
    print("verified: image is in the temp slot")
    if not do_switch:
        print("stopped before IMAGE_SWITCH (no --switch)")
        return
    d.send(frame(0x55, 1, 0x66))
    print("IMAGE_SWITCH sent; device should reset now")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["query"]:
        query()
    elif a[:1] == ["send"] and len(a) >= 2:
        send(a[1], "--switch" in a[2:])
    else:
        sys.exit(__doc__)
