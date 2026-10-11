"""在 WB32 固件二进制里查找 QMK 矩阵引脚表 (ioline_t 数组)。

ChibiOS 下 QMK 的 pin_t 是 ioline_t = GPIO 端口基址 | pad，
所以 const 引脚数组在 .rodata 里表现为连续的 32 位字。
本脚本按给定的引脚顺序搜索这种数组，用来验证某个固件是否使用了同样的矩阵接线。

用法:
    python -I tools/find_matrix_pins.py backup/stock_nut65cu_backup.bin [更多.bin ...]

默认引脚取自 OEM 普通版 NUT65 (hangshengkeji/qmk_firmware keyboards/leku/nut65/keyboard.json)。
已知结果 (2026-10-11): CU 原厂固件和 packOrTake CU V37 固件里都能找到完全相同的行列表，
WB32FQ95 的 GPIO 基址为 A=0x40000000, B=0x40000400, C=0x40000800。
"""
import struct
import sys

COLS = ["C0", "C1", "C2", "C3", "A6", "B10", "B11", "B12", "B13", "B14", "A10", "C6", "C7", "C8", "C9"]
ROWS = ["A1", "A2", "A3", "A4", "C13"]  # 第 6 行是 NO_PIN (0xFFFFFFFF)


def search(data, pins):
    n = len(pins)
    hits = []
    for off in range(0, len(data) - 4 * n + 1, 4):
        words = struct.unpack_from("<%dI" % n, data, off)
        base = {}
        ok = True
        for pin, w in zip(pins, words):
            port, pad = pin[0], int(pin[1:])
            if w & 0x3FF != pad or not (0x40000000 <= w < 0x60000000):
                ok = False
                break
            if base.setdefault(port, w & ~0x3FF) != w & ~0x3FF:
                ok = False
                break
        if ok and len(set(base.values())) == len(base):
            nxt = struct.unpack_from("<I", data, off + 4 * n)[0] if off + 4 * n + 4 <= len(data) else None
            hits.append((off, {k: hex(v) for k, v in base.items()}, nxt))
    return hits


def main(paths):
    for path in paths:
        data = open(path, "rb").read()
        print(path)
        for name, pins in (("cols", COLS), ("rows", ROWS)):
            hits = search(data, pins)
            print(f"  {name}: {len(hits)} hit(s)")
            for off, bases, nxt in hits:
                print(f"    offset 0x{off:05X}  bases={bases}  next_word={hex(nxt) if nxt is not None else '-'}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
