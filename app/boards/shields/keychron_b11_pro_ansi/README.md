# Keychron B11 Pro (ANSI, 65% Alice, foldable) - RTL8762G

Not Keychron's code: Keychron has not published a shield for this board. Pins, the 595 wiring and the
OTA tag (KCZKB11K) were read from the stock 1.0.0 firmware image, see `b11-pro/hardware.md`.

Files here: `keychron_b11_pro_ansi.keymap` (the stock keymap, 6 layers, same bindings as Keychron's 1.0.0
firmware; copy and edit), `.conf`, `.overlay`, `Kconfig.shield`.

Driver changes this shield needs, all behind `CONFIG_SHIELD_KEYCHRON_B11_PRO_ANSI`:
- `app/module/drivers/kscan/kscan_gpio_matrix.c`: `b11_scan()` and the direct-column branch of
  `kscan_matrix_set_all_outputs()` (cols 0-7 behind a 74HC595, cols 8-14 GPIO, 8 rows over both ports).
- `app/src/led/gpio_leds.c`, `app/src/launcher/launcher.c`: treated like the K5 SE2.

Build and flash: see the top-level README.md.
