# B11 Pro in-app DFU (HID interface 2)

The running firmware, stock or custom, has its own firmware-update channel on HID interface 2
(usage page 0x8c, usage 1, report id 0xb1 in / 0xb2 out, 32-byte reports). There is no "enter DFU"
step: the handler is part of the normal application, so a custom image that keeps this code (this tree does)
can always be replaced again over USB, including by the stock image. `b11-pro/b11_dfu.py` implements it.

## Frame

Same format both directions, padded with zeros to 32 bytes:

| byte | meaning |
| --- | --- |
| 0 | 0xAA |
| 1 | 0x55 (no ack) or 0x56 (ack). START requires 0x56 |
| 2 | len = opcode + params + 2 |
| 3 | ~len & 0xFF |
| 4 | sequence number |
| 5 | opcode |
| 6.. | params |
| last 2 | 16-bit little-endian sum of opcode + params |

Replies: `AA 55 len ~len dev_sn rsp_cmd host_sn ack_cmd status [payload] csum16`. `rsp_cmd` 0xA3 for
queries, 0xA1 for acks, status 0 = ok.

## Opcodes

| op | name | notes |
| --- | --- | --- |
| 0x60 | GET_MODEL_INFO | read only: model "KCZKB11K", firmware and hardware version strings |
| 0x61 | GET_DFU_VERSION | read only |
| 0x62 | SET_SC_METHOD | one byte, 0 (no encryption; the only mode implemented) |
| 0x63 | START | header 0xAA56, one param (dfu version byte, 0) |
| 0x64 | SEND_BIN | 16 data bytes per frame in practice; ack carries the running CRC32. Sequence starts at 4 and wraps 255 to 1; resend the same frame on timeout |
| 0x65 | VERIFY_CRC32 | CRC32 twice (8 bytes). Reply byte 0: 0 ok, 1 CRC bad, 2 SHA-256 or customer name bad |
| 0x66 | IMAGE_SWITCH | no reply; marks the staged image ready and resets the chip, the boot code then installs it |
| 0x6E | RTL_PATCH_VERSION | read only |
| 0xF2 | private data | read only |

CRC32 here is zlib's `crc32` without the final xor: `zlib.crc32(data) ^ 0xFFFFFFFF`.

## What the device checks

1. Per frame: header, `len == ~len`, checksum.
2. Running CRC32 over every received byte equals the one the host sends.
3. SHA-256 over the image from the control header (offset 0x1A0) to the end equals the stored hash at 0x180.
4. The 8 bytes at 0x208 equal `KCZKB11K`.

There is no signature, version or anti-rollback check; the stock image itself is unsigned. The transfer only
writes a temporary slot, so a failure before IMAGE_SWITCH leaves the running firmware untouched.
IMAGE_SWITCH is the point of no return: from there the boot code decides.

## Image format

The post-build step (`app/CMakeLists.txt`, `rtk_post_build`) runs Realtek's `prepend_header` over
`app/build/zephyr/zmk.bin`, giving a 0x500-byte header (hash, CRC16, image id 0x37A9, load address
0x0402D000, customer name) followed by the payload. That file is what you flash.
