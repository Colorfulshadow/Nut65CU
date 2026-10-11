"""从 WB32 QMK 固件 bin 中读出 g_led_config（矩阵→灯号、坐标、flags）。

g_led_config 带 weak 属性、放在 .data 里，所以 bin 里存的是它的初始值，
位于 .data 的加载区（crt0 字面量池里的 _textdata_start）。本脚本：
  1. 从 crt0 字面量池（偏移 0x194 起：_textdata_start, _data_start, _data_end）定位 .data 加载区；
  2. 在 .data 里找 matrix_co：ROWS*15 字节，只含 0xFF(NO_LED) 和互不相同的 < N 的灯号；
     其后紧跟 N 个 {x, y} 和 N 个 flags（flags 只取 0/1/2/4/8 组合）。

用法:
    python -I tools/dump_led_config.py backup/stock_nut65cu_backup.bin [--rows 5] [--leds 88]

已知结果 (2026-10-11, CU 原厂固件): ROWS=5, N=88, .data RAM 0x20000d18，
0=T 1=R 2=E 3=W 4=Q 5=Caps 6=LCtrl 7=Win，8-87 灯带（坐标全 0，flags 前 68 个为 4、后 20 个为 0），
矩阵 [4][3] 另指向 67。
"""
import argparse
import struct

COLS = 15


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("--rows", type=int, default=5)
    ap.add_argument("--leds", type=int, default=88)
    a = ap.parse_args()

    d = open(a.bin, "rb").read()
    load, start, end = struct.unpack_from("<III", d, 0x194)
    if not (0x08000000 <= load < 0x08040000 and 0x20000000 <= start < end < 0x20009000):
        raise SystemExit("crt0 字面量池不像 ChibiOS crt0: %08x %08x %08x" % (load, start, end))
    data = d[load - 0x08000000 : load - 0x08000000 + (end - start)]
    print(".data: load 0x%08X, RAM 0x%08X-0x%08X" % (load, start, end))

    mlen, n = a.rows * COLS, a.leds
    for off in range(len(data) - mlen - 3 * n):
        mc = data[off : off + mlen]
        vals = [b for b in mc if b != 0xFF]
        if not vals or max(vals) >= n or len(set(vals)) != len(vals) or mc.count(0xFF) < mlen // 2:
            continue
        flags = data[off + mlen + 2 * n : off + mlen + 3 * n]
        if any(f & ~0x0F for f in flags) or not any(flags):
            continue
        print("g_led_config @ RAM 0x%08X" % (start + off))
        for r in range(a.rows):
            print("  row %d: %s" % (r, " ".join("--" if b == 0xFF else "%2d" % b for b in mc[r * COLS : (r + 1) * COLS])))
        pts = data[off + mlen : off + mlen + 2 * n]
        print("  points:", [(pts[2 * i], pts[2 * i + 1]) for i in range(n)])
        print("  flags: ", list(flags))
        return
    raise SystemExit("没找到；试试别的 --rows / --leds")


if __name__ == "__main__":
    main()
