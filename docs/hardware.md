# NUT65 CU 硬件资料

| 项 | 值 |
|---|---|
| USB | `342D:E545`，制造商 `LEKU`，产品 `NUT65 CU`（普通版 NUT65 为 `342D:E51A`，固件不通用） |
| 刷机模式 | `342D:DFA0` "WB Device in DFU Mode" |
| 主控 | WestberryTech WB32FQ95，Flash 256 KB，SRAM 36 KB |
| 外部 SPI Flash | Zbit `5E3213`，512 KB（实测 JEDEC ID）：0–64 KB 设置（wear leveling 8 KB），64–128 KB 按键计数，128 KB 起事件日志 |
| 矩阵 | cols `C0 C1 C2 C3 A6 B10 B11 B12 B13 B14 A10 C6 C7 C8 C9`，rows `A1 A2 A3 A4 C13`，ROW2COL |
| 灯珠 | WS2812（SPI，DI = B15）共 88 颗 |
| 其他引脚 | LED 电源 A8、LED 升压 A9、USB 电源 B1、插线检测 A7、满电 A15、音乐律动 B6 / B7、无线模块 UART C10 / C11 |

## 灯链

| 索引 | 位置 |
|---|---|
| 0 / 1 / 2 / 3 / 4 | T / R / E / W / Q 键下 |
| 5 / 6 / 7 | Caps / 左 Ctrl / Win 键下 |
| 8 – 87 | 灯带，8 在最左、87 在最右 |

## 如何得出

- 矩阵：在原厂 CU 固件中搜到与普通版完全相同顺序的行列引脚表（`tools/find_matrix_pins.py`）。
- 其他引脚：将原厂 CU 固件反汇编，`keyboard_post_init_kb`、`suspend_power_down_kb`、`suspend_wakeup_init_kb`、
  `lpwr_stop_hook_pre` 与普通版 OEM 源码的编译产物几乎逐指令相同。
- 灯链：从原厂 CU 固件的 `.data` 段读出 `g_led_config`（`tools/dump_led_config.py`），再用 `ledtest` 固件逐颗点亮确认方向。
- 名称：原厂 CU 固件中的 USB / 蓝牙 / 2.4G 字符串。

原厂 CU 固件的矩阵只有 5 行；本固件保留普通版的 6 行定义（第 6 行为 `NO_PIN`），以沿用 82 键的 VIA 布局。

## 无线模块与电脑通信（实测）

| 连接 | Raw HID（VIA 及本固件的 `tools/cu_hid.py`） |
|---|---|
| USB | 可用，往返约 2 ms |
| 2.4G 接收器 | 可用：接收器以 `342D:E545 'NUT65 CU 2.4G'` 提供 0xFF60 接口，往返约 15–20 ms（主控与模块之间的 UART 只有 115200 波特，一包单程就要 3 ms 多） |
| 蓝牙（BLE） | **不可用**：没有 0xFF60 接口；模块另有一个 0xFF00 集合（报告 ID 3，31 字节），写入会被接受，但模块不转给主控（主控端计数为 0） |

切到无线设备时固件会断开 USB，所以 USB 与无线不能同时通信。
电源开关关闭、只靠 USB 供电时，模块测不到电池，经蓝牙向系统报告电量 0%（Windows 会弹出"电量不足"）。
