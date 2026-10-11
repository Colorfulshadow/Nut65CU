"""生成 NUT65 CU 快捷键与功能说明书 docs/manual.svg。

用法:
    python -I tools/gen_manual.py            # 写 docs/manual.svg
键位来自 keyboards/leku/nut65cu/keymaps/default/keymap.c 的 Fn 层（手工对照，改键位后请同步这里的 FN 表）。
"""
import json
import os
import re
from xml.sax.saxutils import escape

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = "'Microsoft YaHei','PingFang SC','Noto Sans CJK SC','Source Han Sans SC',sans-serif"

W = 1680            # canvas width
M = 40              # outer margin
U = 68              # key unit (px)
G = 6               # gap between keys

# category: (accent, light fill, dark text)
CAT = {
    "conn":  ("#2563EB", "#EFF6FF", "#1E40AF"),
    "light": ("#7C3AED", "#F5F3FF", "#5B21B6"),
    "feat":  ("#EA580C", "#FFF7ED", "#9A3412"),
    "sys":   ("#475569", "#F1F5F9", "#1E293B"),
    "power": ("#16A34A", "#F0FDF4", "#166534"),
    "risk":  ("#DC2626", "#FEF2F2", "#991B1B"),
}

# base legend for each matrix position
BASE = {
    (0, 0): "Esc", (0, 1): "1", (0, 2): "2", (0, 3): "3", (0, 4): "4", (0, 5): "5", (0, 6): "6",
    (0, 7): "7", (0, 8): "8", (0, 9): "9", (0, 10): "0", (0, 11): "-", (0, 12): "=", (0, 13): "Backspace",
    (0, 14): "Ins",
    (1, 0): "Tab", (1, 1): "Q", (1, 2): "W", (1, 3): "E", (1, 4): "R", (1, 5): "T", (1, 6): "Y", (1, 7): "U",
    (1, 8): "I", (1, 9): "O", (1, 10): "P", (1, 11): "[", (1, 12): "]", (1, 13): "\\", (1, 14): "Del",
    (2, 0): "Caps", (2, 1): "A", (2, 2): "S", (2, 3): "D", (2, 4): "F", (2, 5): "G", (2, 6): "H", (2, 7): "J",
    (2, 8): "K", (2, 9): "L", (2, 10): ";", (2, 11): "'", (2, 13): "Enter", (2, 14): "PgUp",
    (3, 0): "Shift", (3, 2): "Z", (3, 3): "X", (3, 4): "C", (3, 5): "V", (3, 6): "B", (3, 7): "N", (3, 8): "M",
    (3, 9): ",", (3, 10): ".", (3, 11): "/", (3, 12): "Shift", (3, 13): "↑", (3, 14): "PgDn",
    (4, 0): "Ctrl", (4, 1): "Win", (4, 2): "Alt", (4, 5): "Space", (4, 10): "Alt", (4, 11): "Fn",
    (4, 12): "←", (4, 13): "↓", (4, 14): "→",
}

# Fn layer: (label lines, category)
FN = {
    (0, 0): (["恢复出厂", "长按3秒"], "risk"),
    (0, 13): (["灯带模式"], "light"),
    (0, 14): (["律动校准"], "light"),
    (1, 0): (["` ~"], "sys"),
    (1, 1): (["蓝牙 1"], "conn"), (1, 2): (["蓝牙 2"], "conn"), (1, 3): (["蓝牙 3"], "conn"),
    (1, 4): (["2.4G"], "conn"), (1, 5): (["有线"], "conn"),
    (1, 14): (["音乐律动"], "light"),
    (2, 1): (["WPM"], "feat"),
    (2, 3): (["消抖", "长按3秒"], "sys"),
    (2, 6): (["ScrLk"], "sys"), (2, 7): (["Pause"], "sys"), (2, 8): (["Home"], "sys"), (2, 9): (["End"], "sys"),
    (2, 10): (["番茄钟"], "feat"), (2, 11): (["番茄", "进度"], "feat"),
    (2, 14): (["灯带亮+"], "light"),
    (3, 7): (["全键", "无冲"], "sys"), (3, 8): (["Win/Mac", "长按3秒"], "sys"),
    (3, 12): (["+Esc 刷机"], "risk"),
    (3, 13): (["亮度+"], "light"), (3, 14): (["灯带亮−"], "light"),
    (4, 0): (["电池灯"], "power"), (4, 1): (["Win 锁"], "sys"), (4, 2): (["厂测勿按"], "risk"),
    (4, 5): (["按住：显示电量"], "power"), (4, 10): (["数字⇄F键"], "sys"),
    (4, 12): (["速度−"], "light"), (4, 13): (["亮度−"], "light"), (4, 14): (["速度+"], "light"),
}
for i in range(1, 13):
    FN[(0, i)] = ([f"F{i}"], "sys")

# keys with an indicator LED under them
LEDS = {(1, 5): "T", (1, 4): "R", (1, 3): "E", (1, 2): "W", (1, 1): "Q", (2, 0): "Caps", (4, 0): "LCtrl", (4, 1): "Win"}

MODES = [
    ("rainbow", "彩虹→"), ("rainbow", "←彩虹"), ("rainbow", "中心"), ("breath", "呼吸"),
    ("#EF4444", "红"), ("#F97316", "橙"), ("#EAB308", "黄"), ("#22C55E", "绿"), ("#06B6D4", "青"),
    ("#3B82F6", "蓝"), ("#A855F7", "紫"), ("#FFFFFF", "白"), ("off", "关"),
]

TOMATO, GREEN, WHITE, RED, BLUE, YELLOW = "#F05A3C", "#22C55E", "#FFFFFF", "#EF4444", "#3B82F6", "#EAB308"

# cards: rows are ("kv", chip, text) | ("text", text) | ("modes",) | ("flow", [(color, label), ...])
#        | ("flash", [colors], times, text) | ("led", [(color, letter)], text)
CARDS_TOP = [
    ("连接", "conn", [
        ("kv", "Fn + Q / W / E", "蓝牙 1 / 2 / 3"),
        ("kv", "Fn + R", "2.4G 接收器"),
        ("kv", "Fn + T", "有线 USB"),
        ("kv", "长按 3 秒", "上面任一键长按：进入配对（指示灯快闪）"),
        ("kv", "插上 USB 线", "自动切到有线；拔线回到原来的无线设备"),
        ("text", "设备名：NUT65 CU BT1–3 · NUT65 CU 2.4G"),
    ]),
    ("灯带", "light", [
        ("kv", "Fn + Backspace", "切换灯带模式（13 种，依次为）"),
        ("modes",),
        ("kv", "Fn + ↑ / ↓", "亮度 5 档（Fn + PgUp / PgDn 相同）"),
        ("kv", "Fn + ← / →", "彩虹流动速度 5 档"),
        ("kv", "Fn + Del", "音乐律动（再按换图案，Fn+Backspace 退出）"),
        ("kv", "Fn + Ins", "音乐律动灵敏度校准"),
    ]),
    ("番茄钟 · 打字速度", "feat", [
        ("kv", "Fn + ;", "轻按：开始工作 / 跳到下一阶段"),
        ("kv", "Fn + ; 按住 1 秒", "停止计时"),
        ("kv", "Fn + '", "灯带进度条 显示 / 隐藏"),
        ("flow", [(TOMATO, "工作 25 分"), (GREEN, "休息 5 分"), ("#CBD5E1", "空闲")]),
        ("text", "进度条随时间从右向左缩短；计时中键盘不会休眠。时长可在 VIA 里改。"),
        ("kv", "Fn + A", "WPM 打字速度条：中间向两端，绿→黄→红，100 WPM 满格"),
    ]),
]
CARDS_MID = [
    ("系统", "sys", [
        ("kv", "Fn + M 长按 3 秒", "切换 Win / Mac"),
        ("text", "Mac 模式：Win 键 → Option，左右 Alt → Command"),
        ("kv", "Fn + N", "全键无冲 开 / 关"),
        ("kv", "Fn + D 长按 3 秒", "消抖 1 ms（低延迟）/ 8 ms（默认）"),
        ("kv", "Fn + 右 Alt", "数字行 ⇄ F1–F12（Fn+数字变多媒体键）"),
        ("kv", "Fn + Win", "锁定 Win 键"),
        ("kv", "Fn + H J K L", "ScrLk · Pause · Home · End"),
    ]),
    ("电池 · 休眠", "power", [
        ("kv", "Fn + 空格 按住", "灯带分 10 段显示电量（无线模式）"),
        ("kv", "Fn + 左 Ctrl", "电池指示灯 开 / 关"),
        ("flash", [YELLOW], 2, "电量 15–30%：每 30 秒黄闪 2 次"),
        ("flash", [RED], 4, "电量 < 15%：每 15 秒红闪 4 次"),
        ("text", "无线空闲 5 分钟休眠，按任意键唤醒；有线不休眠。休眠时间与提醒开关在 VIA 里改。"),
        ("text", "电量耗尽 25 秒后深度休眠，插线唤醒。"),
    ]),
    ("灯带闪烁的含义", "light", [
        ("flash", [WHITE], 3, "亮度 / 速度已到头；律动校准"),
        ("flash", [BLUE, WHITE], 3, "切到 Win / 切到 Mac"),
        ("flash", [RED, WHITE], 3, "消抖 1 ms / 8 ms"),
        ("flash", [GREEN, RED], 3, "全键无冲 开 / 关（整块键盘闪）"),
        ("flash", [TOMATO, GREEN], 1, "开始工作 / 开始休息"),
        ("flash", [GREEN, TOMATO], 6, "工作结束 / 休息结束"),
        ("flash", [WHITE], 2, "番茄钟已停止"),
        ("flash", [RED], 1, "WPM 速度条关闭"),
    ]),
]
CARD_BOTTOM = ("恢复 · 刷机 · VIA", "risk", [
    ("kv", "Fn + Esc 长按 3 秒", "恢复出厂设置（整块键盘白闪 3 次）"),
    ("kv", "Fn + 右Shift + Esc", "进入刷机模式"),
    ("kv", "按住 Esc 插线", "清除设置并进入刷机模式（快捷键失效时的退路）"),
    ("text", "VIA：加载 NUT65CU-via.json。Light bar 页：灯带模式 / 亮度 / 速度；Settings 页：休眠时间、插线切有线、低电量提醒、番茄钟时长与进度显示、WPM 显示与满格值。"),
])

LED_CARD = ("指示灯", "conn", [
    ("led", [(BLUE, "Q"), (BLUE, "W"), (BLUE, "E"), (GREEN, "R"), (WHITE, "T")],
     "蓝牙 1–3 · 2.4G · 有线：慢闪 = 回连中，快闪 = 配对中，熄灭 = 已连接"),
    ("led", [(WHITE, "Caps")], "大写锁定开启"),
    ("led", [(WHITE, "Win")], "Win 键已锁定"),
    ("led", [(RED, "Ctrl"), (GREEN, "Ctrl")], "左 Ctrl 下：红 = 充电中 / 电量低时闪，绿 = 已充满"),
    ("text", "键盘图里带圆点的 8 个键下方有指示灯；平时熄灭，只显示状态。"),
])


def tw(s, size):
    """Approximate rendered width of text."""
    w = 0
    for ch in s:
        if ord(ch) > 0x2E80:
            w += size
        elif ch in "→←↑↓⇄·–—":
            w += size * 0.9
        elif ch.isupper() or ch.isdigit():
            w += size * 0.68
        else:
            w += size * 0.56
    return w


def wrap(s, size, width):
    lines, cur = [], ""
    for ch in s:
        if ch in "，。；：、）)" and cur:  # never start a line with punctuation
            cur += ch
            continue
        if tw(cur + ch, size) > width and cur:
            # don't break inside ASCII words if avoidable
            m = re.search(r"[A-Za-z0-9.+/–-]+$", cur)
            if m and ch.isascii() and ch.isalnum() and m.start() > 0:
                lines.append(cur[:m.start()].rstrip())
                cur = m.group(0) + ch
            else:
                lines.append(cur.rstrip())
                cur = ch.lstrip() if ch == " " else ch
        else:
            cur += ch
    if cur:
        lines.append(cur)
    return lines


def text(x, y, s, size=14, fill="#334155", weight=400, anchor="start"):
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" font-weight="{weight}" fill="{fill}" '
            f'text-anchor="{anchor}">{escape(s)}</text>')


def chip(x, y, label, cat, h=26):
    acc, light, dark = CAT[cat]
    w = tw(label, 13) + 18
    return (f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h}" rx="6" fill="{light}" stroke="{acc}" stroke-opacity="0.45"/>'
            + text(x + 9, y + h / 2 + 4.5, label, 13, dark, 600)), w


def swatch(x, y, color, size=14, stroke="#94A3B8"):
    if color == "rainbow":
        fill = "url(#rainbow)"
    elif color == "breath":
        fill = "url(#breath)"
    elif color == "off":
        return f'<rect x="{x}" y="{y}" width="{size}" height="{size}" rx="4" fill="#1E293B"/>'
    else:
        fill = color
    return f'<rect x="{x}" y="{y}" width="{size}" height="{size}" rx="4" fill="{fill}" stroke="{stroke}" stroke-width="1"/>'


ROW_GAP = 10


def layout_card(title, cat, rows, width):
    """Return (height, draw(x, y) -> svg)."""
    pad = 20
    inner = width - 2 * pad
    chip_col = max((tw(r[1], 13) + 18 for r in rows if r[0] == "kv"), default=0)
    chip_col = min(chip_col, inner * 0.46)
    items = []
    y = 58
    for r in rows:
        kind = r[0]
        if kind == "kv":
            lines = wrap(r[2], 14, inner - chip_col - 12)
            h = max(26, len(lines) * 20 + 6)
            items.append((y, kind, r, lines, chip_col))
        elif kind == "text":
            lines = wrap(r[1], 13, inner)
            h = len(lines) * 19
            items.append((y, kind, r, lines, 0))
        elif kind == "modes":
            h = 44
            items.append((y, kind, r, None, 0))
        elif kind == "flow":
            h = 30
            items.append((y, kind, r, None, 0))
        elif kind == "flash":
            dots_w = 108
            lines = wrap(r[3], 14, inner - dots_w)
            h = max(22, len(lines) * 20)
            items.append((y, kind, r, lines, dots_w))
        elif kind == "led":
            chips_w = sum(max(26, tw(lab, 11) + 22) + 4 for _, lab in r[1])
            if chips_w > 150:  # too many chips: text goes below them
                lines = wrap(r[2], 14, inner)
                h = 34 + len(lines) * 20
                items.append((y, kind, r, lines, -1))
            else:
                lines = wrap(r[2], 14, inner - 150)
                h = max(26, len(lines) * 20 + 4)
                items.append((y, kind, r, lines, 150))
        y += h + ROW_GAP
    height = y + pad - ROW_GAP + 6

    def draw(x0, y0, h_override=None):
        acc, light, dark = CAT[cat]
        hh = h_override or height
        out = [f'<rect x="{x0}" y="{y0}" width="{width}" height="{hh}" rx="14" fill="#FFFFFF" stroke="#E2E8F0"/>',
               f'<rect x="{x0}" y="{y0}" width="6" height="{hh}" rx="3" fill="{acc}"/>',
               text(x0 + pad + 4, y0 + 36, title, 19, "#0F172A", 700)]
        for (iy, kind, r, lines, col) in items:
            xx, yy = x0 + pad + 4, y0 + iy
            if kind == "kv":
                svg, _ = chip(xx, yy, r[1], cat)
                out.append(svg)
                for i, ln in enumerate(lines):
                    out.append(text(xx + col + 12, yy + 18 + i * 20, ln, 14, "#334155"))
            elif kind == "text":
                for i, ln in enumerate(lines):
                    out.append(text(xx, yy + 14 + i * 19, ln, 13, "#64748B"))
            elif kind == "modes":
                step = (width - 2 * pad - 4) / len(MODES)
                for i, (c, lab) in enumerate(MODES):
                    cx = xx + i * step + step / 2
                    out.append(swatch(cx - 11, yy, c, 22))
                    out.append(text(cx, yy + 38, lab, 11, "#475569", 400, "middle"))
            elif kind == "flow":
                cx = xx
                for i, (c, lab) in enumerate(r[1]):
                    w = tw(lab, 13) + 34
                    out.append(f'<rect x="{cx}" y="{yy}" width="{w:.1f}" height="26" rx="13" fill="#F8FAFC" stroke="#E2E8F0"/>')
                    out.append(f'<rect x="{cx + 10}" y="{yy + 8}" width="14" height="10" rx="3" fill="{c}"/>')
                    out.append(text(cx + 29, yy + 18, lab, 13, "#334155", 600))
                    cx += w
                    if i < len(r[1]) - 1:
                        out.append(text(cx + 13, yy + 18, "→", 15, "#94A3B8", 700, "middle"))
                        cx += 26
            elif kind == "flash":
                colors, times = r[1], r[2]
                dx = xx
                for j, c in enumerate(colors):
                    if j:
                        out.append(text(dx + 4, yy + 15, "/", 13, "#94A3B8", 400, "middle"))
                        dx += 10
                    out.append(swatch(dx, yy + 3, c, 14))
                    dx += 18
                out.append(text(dx + 2, yy + 15, f"×{times}", 13, "#475569", 600))
                for i, ln in enumerate(lines):
                    out.append(text(xx + col, yy + 15 + i * 20, ln, 14, "#334155"))
            elif kind == "led":
                dx = xx
                for c, lab in r[1]:
                    w = max(26, tw(lab, 11) + 22)
                    out.append(f'<rect x="{dx}" y="{yy}" width="{w:.1f}" height="26" rx="6" fill="#F8FAFC" stroke="#CBD5E1"/>')
                    out.append(f'<circle cx="{dx + 9}" cy="{yy + 13}" r="4.5" fill="{c}" stroke="#94A3B8" stroke-width="1"/>')
                    out.append(text(dx + 16, yy + 17, lab, 11, "#334155", 600))
                    dx += w + 4
                for i, ln in enumerate(lines):
                    if col < 0:
                        out.append(text(xx, yy + 49 + i * 20, ln, 14, "#334155"))
                    else:
                        out.append(text(xx + col, yy + 18 + i * 20, ln, 14, "#334155"))
        return "\n".join(out)

    return height, draw


def keyboard_svg(x0, y0):
    src = open(os.path.join(ROOT, "keyboards/leku/nut65cu/keyboard.json"), encoding="utf-8").read()
    src = re.sub(r"^\s*//.*$", "", src, flags=re.M)
    layout = json.loads(src)["layouts"]["LAYOUT"]["layout"]
    out = []
    for k in layout:
        r, c = k["matrix"]
        if r > 4:
            continue
        x = x0 + k["x"] * U
        y = y0 + k["y"] * U
        w = k.get("w", 1) * U - G
        h = U - G
        fn = FN.get((r, c))
        if fn:
            acc, light, dark = CAT[fn[1]]
            out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="{light}" stroke="{acc}" stroke-opacity="0.55" stroke-width="1.3"/>')
        else:
            out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="#FFFFFF" stroke="#CBD5E1"/>')
        base = BASE.get((r, c), "")
        out.append(text(x + 8, y + 17, base, 12, "#94A3B8", 600))
        if (r, c) in LEDS:
            out.append(f'<circle cx="{x + w - 10}" cy="{y + 11}" r="4" fill="#FDE68A" stroke="#D97706" stroke-width="1"/>')
        if fn:
            lines = fn[0]
            n = len(lines)
            for i, ln in enumerate(lines):
                size = 13 if i == 0 else 11
                if tw(ln, size) > w - 14:
                    size = 11
                yy = y + h - 12 - (n - 1 - i) * 15
                out.append(text(x + w / 2, yy, ln, size, dark if i == 0 else acc, 700 if i == 0 else 500, "middle"))
    width = 16 * U - G
    height = 5 * U - G
    return "\n".join(out), width, height


def main():
    parts = []
    y = M

    # header
    parts.append(text(M, y + 30, "NUT65 CU 快捷键与功能速查", 32, "#0F172A", 800))
    parts.append(text(M, y + 60, "按住 Fn 再按对应键。标“长按”的需持续按住。彩色键 = 有 Fn 功能，圆点 = 键下方有指示灯。", 15, "#64748B"))
    lx = W - M
    legend = [("conn", "连接"), ("light", "灯光"), ("feat", "番茄钟 / WPM"), ("sys", "系统"), ("power", "电池"), ("risk", "恢复 / 刷机")]
    for cat, lab in reversed(legend):
        w = tw(lab, 13) + 42
        lx -= w
        acc, light, dark = CAT[cat]
        parts.append(f'<rect x="{lx}" y="{y + 44}" width="{w - 8}" height="24" rx="12" fill="{light}" stroke="{acc}" stroke-opacity="0.5"/>')
        parts.append(f'<circle cx="{lx + 12}" cy="{y + 56}" r="4.5" fill="{acc}"/>')
        parts.append(text(lx + 22, y + 60.5, lab, 13, dark, 600))
    y += 92

    # keyboard + LED card side by side
    kb_x = M
    kb, kb_w, kb_h = keyboard_svg(kb_x + 18, y + 46)
    panel_w = kb_w + 36
    panel_h = kb_h + 64
    parts.append(f'<rect x="{kb_x}" y="{y}" width="{panel_w}" height="{panel_h}" rx="14" fill="#F8FAFC" stroke="#E2E8F0"/>')
    parts.append(text(kb_x + 22, y + 32, "Fn 层（按住 Fn 时）", 19, "#0F172A", 700))
    parts.append(text(kb_x + panel_w - 20, y + 31, "Mac 模式下 Fn 层相同（Fn+Win 除外）", 13, "#94A3B8", 400, "end"))
    parts.append(kb)
    side_x = kb_x + panel_w + 24
    side_w = W - M - side_x
    h_led, draw_led = layout_card(*LED_CARD, side_w)
    parts.append(draw_led(side_x, y, max(h_led, panel_h)))
    y += max(panel_h, h_led) + 24

    # card grid
    col_w = (W - 2 * M - 2 * 24) / 3
    for row in (CARDS_TOP, CARDS_MID):
        laid = [layout_card(t, c, r, col_w) for t, c, r in row]
        h = max(l[0] for l in laid)
        for i, (_, draw) in enumerate(laid):
            parts.append(draw(M + i * (col_w + 24), y, h))
        y += h + 24
    hb, draw_b = layout_card(*CARD_BOTTOM, W - 2 * M)
    parts.append(draw_b(M, y))
    y += hb + 20
    parts.append(text(W - M, y + 6, "固件：github.com/Colorfulshadow/Nut65CU · 基于航晟 NUT65 开源 QMK", 12, "#94A3B8", 400, "end"))
    H = int(y + 28)

    defs = """<defs>
<linearGradient id="rainbow" x1="0" x2="1" y1="0" y2="0">
<stop offset="0" stop-color="#EF4444"/><stop offset="0.2" stop-color="#F59E0B"/><stop offset="0.4" stop-color="#22C55E"/>
<stop offset="0.6" stop-color="#06B6D4"/><stop offset="0.8" stop-color="#3B82F6"/><stop offset="1" stop-color="#A855F7"/></linearGradient>
<radialGradient id="breath"><stop offset="0" stop-color="#FFFFFF"/><stop offset="0.5" stop-color="#F472B6"/><stop offset="1" stop-color="#6366F1"/></radialGradient>
</defs>"""
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}">\n'
           f'{defs}\n<rect width="{W}" height="{H}" fill="#F1F5F9"/>\n' + "\n".join(parts) + "\n</svg>\n")
    out = os.path.join(ROOT, "docs", "manual.svg")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(svg)
    print(out, W, H)


if __name__ == "__main__":
    main()
