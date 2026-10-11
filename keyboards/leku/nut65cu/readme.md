# NUT65 CU

Weikav / LEKU NUT65 CU (copper case, no per-key RGB: 8 indicator LEDs and an 80-LED light bar).
Ported from the OEM `leku/nut65` source by hangshengkeji, with fixes and light bar features.

* Hardware Supported: NUT65 CU (`342D:E545`)
* Based on: [hangshengkeji/qmk_firmware](https://github.com/hangshengkeji/qmk_firmware) `keyboards/leku/nut65`

Make example for this keyboard (after copying it into the OEM tree and applying `patches/oem-shared-fixes.patch`):

    make leku/nut65cu:default

Keymaps: `default`, and `ledtest` (lights one LED at a time to map the LED chain).

To reset the board into bootloader mode, do one of the following:

* Fn + Right Shift + Esc
* Hold Esc while connecting the USB cable (also erases persistent settings)
