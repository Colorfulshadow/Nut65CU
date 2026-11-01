# NUT65 (regular, per-key RGB) — NUT65CU project firmware

The regular NUT65 (USB `342D:E51A`, 71 key LEDs + 80-LED light bar) built from the same code as the
NUT65 CU firmware (`keyboards/leku/nut65_shared/`): event log, key counters, per-device settings,
pomodoro / WPM / modes, the 0xC5 host protocol 4 with per-key host lights (`KEY_LIGHT`).

* Keyboard Maintainer: [Colorfulshadow](https://github.com/Colorfulshadow)

**Not tested on a real keyboard** (the project has no regular NUT65). The LED map is the OEM's
(`keyboards/leku/nut65`, compared with `tools/scripts/dump_led_config.py`). Recovery: hold Esc while
plugging in (bootmagic) or Fn + Right Shift + Esc enters the bootloader; `leku/nut65:default` is the
OEM firmware to go back to.

    make leku/nut65rgb:default
