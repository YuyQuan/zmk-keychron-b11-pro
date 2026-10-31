# B11 Pro hardware facts

Read from the stock 1.0.0 image (`b11pro_v1.0.0_ansi_ota.bin`) by disassembling its scan code and
decoding its devicetree tables, then confirmed by running the result on a board. No schematic.

| thing | value |
| --- | --- |
| MCU | Realtek RTL8762G (RTL87x2G), Cortex-M33, Zephyr + Realtek HAL |
| USB | 3434:07b0, 3 HID interfaces (keyboard/NKRO/consumer/mouse, VIA raw 0xff60, DFU 0x8c) |
| OTA tag | `KCZKB11K` (8-byte customer name the DFU checks) |
| matrix | 8 rows x 15 columns, no per-key diodes (so ghosting: see `zmk_kscan_ghost_prefer` in `app/src/kscan.c`) |
| rows | B0-B4, B19, B20, A31, active low, weak pull-up (a strong pull-up kills the 595 columns) |
| columns 0-7 | 74HC595: DATA B21, SRCLK A6, RCLK A5. Walking zero, one shift clock per column, column n = output Qn |
| columns 8-14 | direct GPIO outputs |
| mode slider | A8 = 2.4G, A9 = Bluetooth, A10 = USB (A10 must be low at boot or USB stays off), plus a USB-detect pin |
| A26 | Mac/Win switch (stock binds it to `&mo 3`), not a fold sensor |
| B22 | battery charge |
| LEDs | all active low; battery LED `led_rgb` on A29/A25 with 2 channels |
| fold | closing the board cuts power in hardware; the Bluetooth link drops |

Stock's key index table (matrix cell to reading-order key number) is at stock flash 0x4041158 and agrees
with Keychron's launcher layout JSON (`b11-pro/keypeek/b11pro_ansi_launcher_layout.json`).
