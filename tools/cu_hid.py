#!/usr/bin/env python3
"""NUT65 CU raw HID tool (Windows, no third-party packages).

Talks to the keyboard's raw HID interface (usage page 0xFF60, usage 0x61) over USB,
the 2.4G dongle or Bluetooth: VIA commands, plus the CU commands of our firmware
(qmk_firmware/keyboards/leku/nut65cu/cu_host.h).

    python tools/cu_hid.py info                  state, battery, flash, counters
    python tools/cu_hid.py log [--csv f.csv]     battery / sleep / connection log
    python tools/cu_hid.py keys [--svg map.svg]  key press counters and heat map
    python tools/cu_hid.py profile [...]         per-device settings
    python tools/cu_hid.py mode [normal|eco|game]
    python tools/cu_hid.py list | ping | echo    interfaces, link round trips
    python tools/cu_hid.py flash-dump f.bin      back up the external flash
"""

import argparse
import csv
import ctypes
import ctypes.wintypes as wt
import json
import os
import statistics
import struct
import sys
import time

RAW_USAGE_PAGE = 0xFF60
RAW_USAGE = 0x61
BLE_HID_SERVICE = "{00001812-"  # HID over GATT: Bluetooth connections
BLE_USAGE_PAGE = 0xFF00          # the module's vendor collection, 31-byte reports with an ID;
                                 # writes are accepted but never reach the keyboard's MCU

# ---------------------------------------------------------------- Win32 HID

setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
hidapi = ctypes.WinDLL("hid", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class GUID(ctypes.Structure):
    _fields_ = [("Data1", wt.DWORD), ("Data2", wt.WORD), ("Data3", wt.WORD), ("Data4", ctypes.c_ubyte * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("InterfaceClassGuid", GUID), ("Flags", wt.DWORD), ("Reserved", ctypes.c_void_p)]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [("Size", wt.ULONG), ("VendorID", wt.USHORT), ("ProductID", wt.USHORT), ("VersionNumber", wt.USHORT)]


class HIDP_CAPS(ctypes.Structure):
    _fields_ = [
        ("Usage", wt.USHORT),
        ("UsagePage", wt.USHORT),
        ("InputReportByteLength", wt.USHORT),
        ("OutputReportByteLength", wt.USHORT),
        ("FeatureReportByteLength", wt.USHORT),
        ("Reserved", wt.USHORT * 17),
        ("Fields", wt.USHORT * 10),
    ]


class OVERLAPPED(ctypes.Structure):
    _fields_ = [
        ("Internal", ctypes.c_void_p),
        ("InternalHigh", ctypes.c_void_p),
        ("Offset", wt.DWORD),
        ("OffsetHigh", wt.DWORD),
        ("hEvent", wt.HANDLE),
    ]


DIGCF_PRESENT = 0x02
DIGCF_DEVICEINTERFACE = 0x10
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_RW = 0x03
OPEN_EXISTING = 3
FILE_FLAG_OVERLAPPED = 0x40000000
INVALID_HANDLE = ctypes.c_void_p(-1).value
ERROR_IO_PENDING = 997
WAIT_OBJECT_0 = 0

setupapi.SetupDiGetClassDevsW.restype = wt.HANDLE
setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(GUID), wt.LPCWSTR, wt.HWND, wt.DWORD]
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(GUID), wt.DWORD, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [wt.HANDLE, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA), ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [wt.HANDLE]
kernel32.CreateFileW.restype = wt.HANDLE
kernel32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE]
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.CreateEventW.restype = wt.HANDLE
kernel32.CreateEventW.argtypes = [ctypes.c_void_p, wt.BOOL, wt.BOOL, wt.LPCWSTR]
kernel32.ReadFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.POINTER(OVERLAPPED)]
kernel32.WriteFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.POINTER(OVERLAPPED)]
kernel32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
kernel32.GetOverlappedResult.argtypes = [wt.HANDLE, ctypes.POINTER(OVERLAPPED), ctypes.POINTER(wt.DWORD), wt.BOOL]
kernel32.CancelIo.argtypes = [wt.HANDLE]
hidapi.HidD_GetHidGuid.argtypes = [ctypes.POINTER(GUID)]
hidapi.HidD_GetAttributes.argtypes = [wt.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
hidapi.HidD_GetProductString.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.ULONG]
hidapi.HidD_GetPreparsedData.argtypes = [wt.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
hidapi.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
hidapi.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)]


def _interface_paths():
    guid = GUID()
    hidapi.HidD_GetHidGuid(ctypes.byref(guid))
    info = setupapi.SetupDiGetClassDevsW(ctypes.byref(guid), None, None, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
    try:
        i = 0
        while True:
            ifd = SP_DEVICE_INTERFACE_DATA()
            ifd.cbSize = ctypes.sizeof(ifd)
            if not setupapi.SetupDiEnumDeviceInterfaces(info, None, ctypes.byref(guid), i, ctypes.byref(ifd)):
                return
            i += 1
            need = wt.DWORD()
            setupapi.SetupDiGetDeviceInterfaceDetailW(info, ctypes.byref(ifd), None, 0, ctypes.byref(need), None)
            buf = ctypes.create_string_buffer(need.value)
            # SP_DEVICE_INTERFACE_DETAIL_DATA_W.cbSize: 8 on 64-bit, 6 on 32-bit
            struct.pack_into("I", buf, 0, 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6)
            if setupapi.SetupDiGetDeviceInterfaceDetailW(info, ctypes.byref(ifd), buf, need, None, None):
                yield ctypes.wstring_at(ctypes.addressof(buf) + 4)
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(info)


class RawDevice:
    def __init__(self, path, vid, pid, product, in_len, out_len, ble=False):
        self.path, self.vid, self.pid, self.product = path, vid, pid, product
        self.in_len, self.out_len = in_len, out_len
        self.ble = ble
        self.report_id = 0
        self.handle = None

    def __repr__(self):
        if self.ble:
            return f"Bluetooth (BLE) vendor collection in={self.in_len} out={self.out_len}"
        return f"{self.vid:04X}:{self.pid:04X} '{self.product}' in={self.in_len} out={self.out_len}"

    def open(self):
        h = kernel32.CreateFileW(self.path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_RW, None, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, None)
        if h == INVALID_HANDLE:
            raise OSError(ctypes.get_last_error(), f"cannot open {self.path}")
        self.handle = h
        self.event = kernel32.CreateEventW(None, True, False, None)
        if self.ble:
            self._find_report_id()
        return self

    def _find_report_id(self):
        # Windows rejects writes with the wrong report ID; VIA's get_protocol_version is harmless.
        for rid in range(1, 16):
            self.report_id = rid
            try:
                self.write([VIA_GET_PROTOCOL_VERSION], 300)
                return
            except (OSError, TimeoutError):
                pass
        raise OSError(0, "no usable report ID on the Bluetooth vendor collection")

    def close(self):
        if self.handle:
            kernel32.CloseHandle(self.handle)
            kernel32.CloseHandle(self.event)
            self.handle = None

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    def _io(self, fn, buf, size, timeout_ms):
        ov = OVERLAPPED()
        ov.hEvent = self.event
        done = wt.DWORD()
        if not fn(self.handle, buf, size, None, ctypes.byref(ov)):
            err = ctypes.get_last_error()
            if err != ERROR_IO_PENDING:
                raise OSError(err, "HID I/O failed")
            if kernel32.WaitForSingleObject(self.event, timeout_ms) != WAIT_OBJECT_0:
                kernel32.CancelIo(self.handle)
                kernel32.GetOverlappedResult(self.handle, ctypes.byref(ov), ctypes.byref(done), True)
                return None
        if not kernel32.GetOverlappedResult(self.handle, ctypes.byref(ov), ctypes.byref(done), True):
            raise OSError(ctypes.get_last_error(), "HID I/O failed")
        return done.value

    def write(self, data, timeout_ms=1000):
        payload = bytes(data)[: self.out_len - 1].ljust(self.out_len - 1, b"\0")
        buf = ctypes.create_string_buffer(bytes([self.report_id]) + payload, self.out_len)
        if self._io(kernel32.WriteFile, buf, self.out_len, timeout_ms) is None:
            raise TimeoutError("write timed out")

    def read(self, timeout_ms=1000):
        buf = ctypes.create_string_buffer(self.in_len)
        n = self._io(kernel32.ReadFile, buf, self.in_len, timeout_ms)
        if n is None:
            return None
        return buf.raw[1:n]  # drop report id

    def request(self, data, timeout_ms=1000):
        """Send one 32-byte report and wait for the reply to the same command byte."""
        self.write(data, timeout_ms)
        deadline = time.monotonic() + timeout_ms / 1000
        while True:
            left = int((deadline - time.monotonic()) * 1000)
            if left <= 0:
                return None
            reply = self.read(left)
            if reply is None:
                return None
            if reply[:1] == bytes(data[:1]) or reply[:1] == b"\xff":
                return reply


def find_raw_devices():
    found = []
    for path in _interface_paths():
        h = kernel32.CreateFileW(path, 0, FILE_SHARE_RW, None, OPEN_EXISTING, 0, None)
        if h == INVALID_HANDLE:
            continue
        try:
            pre = ctypes.c_void_p()
            if not hidapi.HidD_GetPreparsedData(h, ctypes.byref(pre)):
                continue
            caps = HIDP_CAPS()
            hidapi.HidP_GetCaps(pre, ctypes.byref(caps))
            hidapi.HidD_FreePreparsedData(pre)
            ble = BLE_HID_SERVICE in path.lower() and caps.UsagePage == BLE_USAGE_PAGE and caps.OutputReportByteLength == 32
            if not ble and (caps.UsagePage != RAW_USAGE_PAGE or caps.Usage != RAW_USAGE):
                continue
            attrs = HIDD_ATTRIBUTES()
            attrs.Size = ctypes.sizeof(attrs)
            hidapi.HidD_GetAttributes(h, ctypes.byref(attrs))
            name = ctypes.create_unicode_buffer(128)
            product = name.value if hidapi.HidD_GetProductString(h, name, ctypes.sizeof(name)) else "?"
            found.append(RawDevice(path, attrs.VendorID, attrs.ProductID, product, caps.InputReportByteLength, caps.OutputReportByteLength, ble))
        finally:
            kernel32.CloseHandle(h)
    return found


# ---------------------------------------------------------------- CU protocol (cu_host.h)

VIA_GET_PROTOCOL_VERSION = 0x01
VIA_GET_KEYBOARD_VALUE = 0x02
VIA_UPTIME = 0x01

CU = 0xC5
CU_INFO, CU_BUILD, CU_TIME, CU_ECHO, CU_DIAG = 0x01, 0x02, 0x03, 0x04, 0x05
CU_LOG_INFO, CU_LOG_READ, CU_LOG_MARK = 0x10, 0x11, 0x12
CU_KEYS_INFO, CU_KEYS_READ, CU_KEYS_SAVE, CU_KEYS_CLEAR = 0x20, 0x21, 0x22, 0x23
CU_PROFILE_GET, CU_PROFILE_SET, CU_PROFILE_ENABLE = 0x30, 0x31, 0x32
CU_FLASH_READ, CU_FLASH_FORMAT = 0x40, 0x41
KEYS_CLEAR_MAGIC = 0x5AA5
FORMAT_MAGIC = 0x21544D46

STATUS = {0: "ok", 1: "unknown command (old firmware?)", 2: "bad argument", 3: "external flash missing or locked", 4: "failed"}
DEVS = ["USB", "BT1", "BT2", "BT3", "BT4", "BT5", "2.4G"]
MD_STATE = {0: "none", 1: "pairing", 2: "connected", 3: "disconnected", 4: "rejected"}
USB_STATE = {0: "uninit", 1: "stopped", 2: "ready", 3: "selected", 4: "active", 5: "suspended"}
STORE = {0: "no flash", 1: "LOCKED (unknown data, see 'format')", 2: "ok"}
SLEEP = ["never", "1 min", "3 min", "5 min", "10 min", "20 min", "30 min", "default (5 min)"]
MODES = ["normal", "power save", "game"]
MODE_ARGS = {"normal": 0, "eco": 1, "game": 2}
VIA_CUSTOM_SET, VIA_CUSTOM_GET, ID_CU_MODE = 0x07, 0x08, 0x10
WAKE = {0: "none", 1: "key", 2: "module UART", 3: "cable", 4: "USB", 5: "one key", 6: "encoder", 7: "switch"}
SLEEP_REASON = {0: "idle", 1: "requested", 2: "battery empty"}
LOG_TYPES = {1: "BOOT", 2: "SLEEP", 3: "WAKE", 4: "BATTERY", 5: "CABLE", 6: "CHARGED", 7: "DEVICE", 8: "LINK", 9: "TIME", 10: "MARK", 11: "MODE"}
JEDEC_VENDOR = {0xEF: "Winbond", 0xC8: "GigaDevice", 0x85: "Puya", 0x0B: "XTX", 0x20: "XMC", 0x5E: "Zbit", 0x68: "Boya", 0x9D: "ISSI", 0xC2: "Macronix", 0x1C: "EON"}

LAYOUT = [  # (row, col, x, y, width, label), from keyboard.json and the default keymap
    (0, 0, 0, 0, 1, "Esc"), (0, 1, 1, 0, 1, "1"), (0, 2, 2, 0, 1, "2"), (0, 3, 3, 0, 1, "3"),
    (0, 4, 4, 0, 1, "4"), (0, 5, 5, 0, 1, "5"), (0, 6, 6, 0, 1, "6"), (0, 7, 7, 0, 1, "7"),
    (0, 8, 8, 0, 1, "8"), (0, 9, 9, 0, 1, "9"), (0, 10, 10, 0, 1, "0"), (0, 11, 11, 0, 1, "-"),
    (0, 12, 12, 0, 1, "="), (0, 13, 13, 0, 2, "Bksp"), (0, 14, 15, 0, 1, "Ins"), (1, 0, 0, 1, 1.5, "Tab"),
    (1, 1, 1.5, 1, 1, "Q"), (1, 2, 2.5, 1, 1, "W"), (1, 3, 3.5, 1, 1, "E"), (1, 4, 4.5, 1, 1, "R"),
    (1, 5, 5.5, 1, 1, "T"), (1, 6, 6.5, 1, 1, "Y"), (1, 7, 7.5, 1, 1, "U"), (1, 8, 8.5, 1, 1, "I"),
    (1, 9, 9.5, 1, 1, "O"), (1, 10, 10.5, 1, 1, "P"), (1, 11, 11.5, 1, 1, "["), (1, 12, 12.5, 1, 1, "]"),
    (1, 13, 13.5, 1, 1.5, "\\"), (1, 14, 15, 1, 1, "Del"), (2, 0, 0, 2, 1.75, "Caps"), (2, 1, 1.75, 2, 1, "A"),
    (2, 2, 2.75, 2, 1, "S"), (2, 3, 3.75, 2, 1, "D"), (2, 4, 4.75, 2, 1, "F"), (2, 5, 5.75, 2, 1, "G"),
    (2, 6, 6.75, 2, 1, "H"), (2, 7, 7.75, 2, 1, "J"), (2, 8, 8.75, 2, 1, "K"), (2, 9, 9.75, 2, 1, "L"),
    (2, 10, 10.75, 2, 1, ";"), (2, 11, 11.75, 2, 1, "'"), (2, 13, 12.75, 2, 2.25, "Enter"),
    (2, 14, 15, 2, 1, "PgUp"), (3, 0, 0, 3, 2.25, "Shift"), (3, 2, 2.25, 3, 1, "Z"), (3, 3, 3.25, 3, 1, "X"),
    (3, 4, 4.25, 3, 1, "C"), (3, 5, 5.25, 3, 1, "V"), (3, 6, 6.25, 3, 1, "B"), (3, 7, 7.25, 3, 1, "N"),
    (3, 8, 8.25, 3, 1, "M"), (3, 9, 9.25, 3, 1, ","), (3, 10, 10.25, 3, 1, "."), (3, 11, 11.25, 3, 1, "/"),
    (3, 12, 12.25, 3, 1.75, "Shift"), (3, 13, 14, 3, 1, "Up"), (3, 14, 15, 3, 1, "PgDn"),
    (4, 0, 0, 4, 1.25, "Ctrl"), (4, 1, 1.25, 4, 1.25, "Win"), (4, 2, 2.5, 4, 1.25, "Alt"),
    (4, 5, 3.75, 4, 6.25, "Space"), (4, 10, 10, 4, 1.25, "Alt"), (4, 11, 11.25, 4, 1.25, "Fn"),
    (4, 12, 13, 4, 1, "Left"), (4, 13, 14, 4, 1, "Down"), (4, 14, 15, 4, 1, "Right"),
]


class CuError(Exception):
    pass


def cu(dev, sub, args=b"", timeout=1000):
    """Send a CU command; returns the payload (28 bytes, see cu_host.h)."""
    reply = dev.request(bytes([CU, sub]) + bytes(args), timeout)
    if reply is None:
        raise CuError("no reply (keyboard asleep, or firmware without the CU commands?)")
    if reply[0] == 0xFF:
        raise CuError("command not understood: this firmware has no CU commands")
    if reply[2] != 0:
        raise CuError(STATUS.get(reply[2], f"status {reply[2]}"))
    return reply[3:31]


def pick_device(args):
    devs = find_raw_devices()
    if args.vidpid:
        vid, pid = (int(x, 16) for x in args.vidpid.split(":"))
        devs = [d for d in devs if d.vid == vid and d.pid == pid]
    if devs and all(d.ble for d in devs):
        sys.exit("only a Bluetooth connection found: the module doesn't pass raw HID to the keyboard over "
                 "Bluetooth (tested 2026-10-12). Use USB or the 2.4G dongle.")
    devs = [d for d in devs if not d.ble]
    if not devs:
        sys.exit("no raw HID interface (usage page 0xFF60) found")
    if len(devs) > 1 and not args.vidpid:
        print("several raw HID interfaces, using the first (pick one with --dev VID:PID):")
        for d in devs:
            print("  ", d)
    return devs[0]


def link_name(code):
    return f"USB {USB_STATE.get(code - 0x10, code)}" if code >= 0x10 else MD_STATE.get(code, str(code))


def ms_str(ms):
    s = ms // 1000
    return f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}"


# ---------------------------------------------------------------- commands

def cmd_list(args):
    devs = find_raw_devices()
    if not devs:
        print("no raw HID interface (usage page 0xFF60) found")
    for d in devs:
        print(d)
        print("   ", d.path)


def cmd_ping(args):
    dev = pick_device(args)
    print("using", dev)
    with dev:
        reply = dev.request([VIA_GET_PROTOCOL_VERSION], args.timeout)
        if reply is None:
            sys.exit("no reply to get_protocol_version")
        print(f"VIA protocol 0x{reply[1]:02X}{reply[2]:02X}")
        times, lost, last_up = [], 0, None
        for _ in range(args.count):
            t0 = time.perf_counter()
            reply = dev.request([VIA_GET_KEYBOARD_VALUE, VIA_UPTIME], args.timeout)
            if reply is None or reply[0] != VIA_GET_KEYBOARD_VALUE:
                lost += 1
                continue
            times.append((time.perf_counter() - t0) * 1000)
            last_up = struct.unpack(">I", reply[2:6])[0]
        if times:
            print(f"{len(times)}/{args.count} replies, round trip ms: min {min(times):.1f}  median {statistics.median(times):.1f}  max {max(times):.1f}")
            print(f"keyboard uptime {last_up / 1000:.1f} s")
        if lost:
            print(f"{lost} requests without a reply")


def cmd_echo(args):
    """Round trips with a full 29-byte payload, checking it comes back intact."""
    dev = pick_device(args)
    print("using", dev)
    with dev:
        times, bad = [], 0
        for i in range(args.count):
            payload = bytes((i + k) & 0xFF for k in range(29))
            t0 = time.perf_counter()
            try:
                reply = cu(dev, CU_ECHO, payload, args.timeout)
            except CuError as e:
                sys.exit(str(e))
            times.append((time.perf_counter() - t0) * 1000)
            bad += reply[:28] != payload[1:29]
        print(f"{args.count} round trips, ms: min {min(times):.1f}  median {statistics.median(times):.1f}  max {max(times):.1f}; corrupted: {bad}")


def read_info(dev):
    p = cu(dev, CU_INFO)
    build = cu(dev, CU_BUILD).split(b"\0")[0].decode()
    flags = p[4]
    return {
        "build": build,
        "devs": p[1], "link": p[2], "bat": p[3],
        "cable": bool(flags & 1), "full": bool(flags & 2), "nkro": bool(flags & 4), "mac": bool(flags & 8),
        "win_lock": bool(flags & 16), "f_row": bool(flags & 32), "profiles": bool(flags & 64),
        "sleep": p[5] & 7, "mode": p[5] >> 4, "store": p[6], "flash_log2": p[7],
        "uptime": struct.unpack_from("<I", p, 8)[0], "boot": struct.unpack_from("<H", p, 12)[0],
        "log_next": struct.unpack_from("<I", p, 14)[0], "keys_total": struct.unpack_from("<I", p, 18)[0],
        "jedec": (p[22] << 16) | (p[23] << 8) | p[24], "profile_slot": p[25], "debounce": p[26],
        "bar_mode": p[27] & 15, "bar_level": p[27] >> 4 & 7,
    }


def cmd_info(args):
    dev = pick_device(args)
    with dev:
        try:
            i = read_info(dev)
        except CuError as e:
            sys.exit(str(e))
    jedec = i["jedec"]
    print(f"interface   {dev}")
    print(f"firmware    {i['build']}   boot #{i['boot']}   awake {ms_str(i['uptime'])}")
    print(f"mode        {MODES[i['mode']] if i['mode'] < len(MODES) else i['mode']}")
    print(f"link        {DEVS[i['devs']] if i['devs'] < len(DEVS) else i['devs']}, {link_name(i['link'])}")
    charge = "charged" if i["full"] else "charging" if i["cable"] else "on battery"
    print(f"battery     {i['bat']}% ({charge}), sleep after {SLEEP[i['sleep'] & 7]}")
    print(f"keys        {'Mac' if i['mac'] else 'Win'}, Win lock {'on' if i['win_lock'] else 'off'}, NKRO {'on' if i['nkro'] else 'off'}, "
          f"number row {'F1-F12' if i['f_row'] else 'digits'}, debounce {i['debounce']} ms")
    slot = DEVS[i["profile_slot"]] if i["profile_slot"] < len(DEVS) else "-"
    print(f"per-device  {'on, active: ' + slot if i['profiles'] else 'off'}")
    print(f"light bar   mode {i['bar_mode']}, brightness {i['bar_level'] + 1}/5")
    size = f"{(1 << i['flash_log2']) // 1024} KB" if i["flash_log2"] else "-"
    print(f"flash       {JEDEC_VENDOR.get(jedec >> 16, 'unknown')} {jedec:06X}, {size}, {STORE.get(i['store'], i['store'])}")
    print(f"stats       {i['keys_total']} key presses, {i['log_next']} log records written")


def cmd_diag(args):
    """What the wireless module has sent the MCU (keyboards/linker/wireless/module.h md_diag_t)."""
    dev = pick_device(args)
    with dev:
        p = cu(dev, CU_DIAG)
    frames, bad_sum, unknown, raw_rx, raw_tx = struct.unpack_from("<5H", p, 2)
    unknown_last, raw_len = p[12], p[13]
    last = [(p[14 + 2 * i], p[15 + 2 * i]) for i in range(4)]
    print(f"module firmware 0x{p[0]:02X}, link {link_name(p[1])}")
    print(f"frames from the module: {frames} ok, {bad_sum} bad checksum, {unknown} stray bytes (last 0x{unknown_last:02X})")
    print(f"raw packets: {raw_rx} received (last length {raw_len}), {raw_tx} sent")
    print("last frames (command, first byte): " + ", ".join(f"{c:02X} {b:02X}" for c, b in last))


def cmd_time(args):
    dev = pick_device(args)
    with dev:
        cu(dev, CU_TIME, struct.pack("<I", int(time.time())))
    print("time sent (logged as a TIME record)")


def cmd_mark(args):
    dev = pick_device(args)
    with dev:
        cu(dev, CU_LOG_MARK, struct.pack("<H", args.value & 0xFFFF))
    print(f"MARK {args.value} logged")


# ---- event log

def cache_path():
    base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    return os.path.join(base, "nut65cu", "log_cache.json")


def decode_record(raw):
    check, typ, boot, seq, ms, bat, state, arg = struct.unpack("<BBHIIBBH", raw)
    rec = {"seq": seq, "boot": boot, "ms": ms, "type": typ, "bat": bat, "state": state, "arg": arg}
    if typ == 9:
        rec["unix"] = struct.unpack_from("<I", raw, 12)[0]
    return rec


def describe(rec):
    t, arg = rec["type"], rec["arg"]
    if t == 2:
        return f"sleep ({SLEEP_REASON.get(arg, arg)})"
    if t == 3:
        extra = f", {arg >> 8} wake-ups straight back to sleep" if arg >> 8 else ""
        return f"wake by {WAKE.get(arg & 0xFF, arg & 0xFF)}{extra}"
    if t == 5:
        return "cable plugged in" if arg else "cable unplugged"
    if t == 7:
        return f"device -> {DEVS[arg] if arg < len(DEVS) else arg}"
    if t == 8:
        return f"link {link_name(arg)}"
    if t == 9:
        return "host time " + time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(rec["unix"]))
    if t == 10:
        return f"mark {arg}"
    if t == 11:
        return f"mode -> {MODES[arg] if arg < len(MODES) else arg}"
    return LOG_TYPES.get(t, f"type {t}").lower()


def state_str(rec):
    if rec["type"] == 9:
        return ""
    s = rec["state"]
    dev = DEVS[s & 7] if (s & 7) < len(DEVS) else "?"
    return f"{rec['bat']:3d}% {dev:4s} {'linked' if s & 8 else '      '} {'cable' if s & 16 else '     '}{' full' if s & 32 else ''}"


def fetch_log(dev, progress=True):
    p = cu(dev, CU_LOG_INFO)
    store, capacity, next_seq, boot = p[0], *struct.unpack_from("<IIH", p, 1)
    if store != 2:
        raise CuError(f"log unavailable: {STORE.get(store, store)}")
    path = cache_path()
    cache = {}
    try:
        with open(path, encoding="utf8") as f:
            cache = {int(k): v for k, v in json.load(f).items()}
    except (OSError, ValueError):
        pass
    if cache and max(cache) >= next_seq:  # flash was formatted / replaced
        cache = {}
    first = max(0, next_seq - capacity)
    cache = {k: v for k, v in cache.items() if k >= first}
    todo = [s for s in range(first, next_seq) if s not in cache]
    for n, seq in enumerate(todo):
        p = cu(dev, CU_LOG_READ, struct.pack("<I", seq))
        if p[0]:
            cache[seq] = p[1:17].hex()
        if progress and n % 200 == 199:
            print(f"  read {n + 1}/{len(todo)} records...", file=sys.stderr)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf8") as f:
        json.dump({str(k): v for k, v in sorted(cache.items())}, f)
    return [decode_record(bytes.fromhex(cache[k])) for k in sorted(cache)], capacity, next_seq


def cmd_log(args):
    dev = pick_device(args)
    with dev:
        try:
            if not args.no_time:
                cu(dev, CU_TIME, struct.pack("<I", int(time.time())))
            records, capacity, next_seq = fetch_log(dev)
        except CuError as e:
            sys.exit(str(e))
    print(f"{len(records)} records (ring of {capacity}, {next_seq} written in total)")
    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf8") as f:
            w = csv.writer(f)
            w.writerow(["seq", "boot", "awake_s", "type", "event", "battery", "device", "linked", "cable", "full", "arg"])
            for r in records:
                s = r["state"]
                w.writerow([r["seq"], r["boot"], r["ms"] / 1000, LOG_TYPES.get(r["type"], r["type"]), describe(r),
                            "" if r["type"] == 9 else r["bat"], DEVS[s & 7] if (s & 7) < len(DEVS) else s & 7,
                            int(bool(s & 8)), int(bool(s & 16)), int(bool(s & 32)), r["arg"]])
        print(f"written {args.csv}")
    shown = records if args.all else records[-args.tail:]
    for r in shown:
        print(f"#{r['seq']:<6d} boot {r['boot']:<4d} {ms_str(r['ms']):>9s}  {state_str(r):28s} {describe(r)}")
    summarize(records)


def summarize(records):
    """Per boot: how often it slept and what woke it."""
    boots = {}
    for r in records:
        b = boots.setdefault(r["boot"], {"sleep": 0, "wake": {}, "resleep": 0, "bat": [], "awake": 0})
        b["awake"] = max(b["awake"], r["ms"])
        if r["type"] == 2:
            b["sleep"] += 1
        elif r["type"] == 3:
            cause = WAKE.get(r["arg"] & 0xFF, r["arg"] & 0xFF)
            b["wake"][cause] = b["wake"].get(cause, 0) + 1
            b["resleep"] += r["arg"] >> 8
        if r["type"] != 9 and r["bat"] <= 100 and not r["state"] & 16:
            b["bat"].append(r["bat"])
    if not boots:
        return
    print("\nper boot: awake time, sleeps, wake causes, battery on battery power")
    for k in sorted(boots)[-10:]:
        b = boots[k]
        wakes = ", ".join(f"{c} {n}" for c, n in b["wake"].items()) or "-"
        bat = f"{b['bat'][0]}% -> {b['bat'][-1]}%" if b["bat"] else "-"
        resleep = f", {b['resleep']} module wake-ups" if b["resleep"] else ""
        print(f"  boot {k:<4d} awake {ms_str(b['awake']):>9s}  sleeps {b['sleep']:<3d} wakes: {wakes}{resleep}  battery {bat}")


# ---- key counters

def read_keys(dev):
    p = cu(dev, CU_KEYS_INFO)
    rows, cols = p[0], p[1]
    total, saved, dirty = struct.unpack_from("<I", p, 2)[0], struct.unpack_from("<I", p, 6)[0], p[10]
    counts = []
    while len(counts) < rows * cols:
        q = cu(dev, CU_KEYS_READ, bytes([len(counts)]))
        counts += struct.unpack_from(f"<{q[1]}I", q, 2)
    return rows, cols, counts, total, saved, dirty


def heat_color(f):
    # white -> yellow -> orange -> red
    stops = [(255, 255, 255), (255, 230, 120), (255, 150, 60), (215, 40, 40)]
    f = max(0.0, min(1.0, f)) * (len(stops) - 1)
    i = min(int(f), len(stops) - 2)
    t = f - i
    return tuple(round(a + (b - a) * t) for a, b in zip(stops[i], stops[i + 1]))


def write_svg(path, counts, cols):
    unit, pad = 60, 4
    peak = max(counts) or 1
    width = 16 * unit + 2 * pad
    height = 5 * unit + 2 * pad + 30
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" font-family="Segoe UI, sans-serif">',
             f'<rect width="{width}" height="{height}" fill="#f6f6f4"/>']
    for row, col, x, y, w, label in LAYOUT:
        n = counts[row * cols + col]
        r, g, b = heat_color((n / peak) ** 0.5)  # sqrt: rare keys still show
        X, Y, W = pad + x * unit, pad + y * unit, w * unit
        parts.append(f'<rect x="{X + 2}" y="{Y + 2}" width="{W - 4}" height="{unit - 4}" rx="6" fill="rgb({r},{g},{b})" stroke="#999"/>')
        esc = label.replace("&", "&amp;").replace("<", "&lt;")
        parts.append(f'<text x="{X + W / 2}" y="{Y + 25}" text-anchor="middle" font-size="15" fill="#222">{esc}</text>')
        parts.append(f'<text x="{X + W / 2}" y="{Y + 45}" text-anchor="middle" font-size="12" fill="#444">{n}</text>')
    parts.append(f'<text x="{pad + 4}" y="{height - 10}" font-size="13" fill="#555">NUT65 CU key presses, total {sum(counts)}, '
                 f'{time.strftime("%Y-%m-%d %H:%M")}</text></svg>')
    with open(path, "w", encoding="utf8") as f:
        f.write("\n".join(parts))


def cmd_keys(args):
    dev = pick_device(args)
    with dev:
        try:
            rows, cols, counts, total, saved, dirty = read_keys(dev)
        except CuError as e:
            sys.exit(str(e))
    print(f"{total} key presses ({'unsaved presses pending' if dirty else 'all saved'}, {saved} snapshots written)")
    labelled = sorted(((counts[r * cols + c], label) for r, c, _, _, _, label in LAYOUT), reverse=True)
    top = "  ".join(f"{label} {n}" for n, label in labelled[:args.top] if n)
    if top:
        print("most used:", top)
    # Terminal heat map: one line per row, keys in order.
    peak = max(counts) or 1
    shades = " .:-=+*#%@"
    for y in range(5):
        cells = []
        for r, c, x, yy, w, label in LAYOUT:
            if yy == y:
                n = counts[r * cols + c]
                cells.append(f"{label[:5]:>5s}{shades[min(9, int(9 * (n / peak) ** 0.5 + 0.999)) if n else 0]}")
        print(" ".join(cells))
    if args.svg:
        write_svg(args.svg, counts, cols)
        print(f"written {args.svg}")
    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf8") as f:
            w = csv.writer(f)
            w.writerow(["row", "col", "key", "presses"])
            for r, c, _, _, _, label in LAYOUT:
                w.writerow([r, c, label, counts[r * cols + c]])
        print(f"written {args.csv}")


def cmd_keys_save(args):
    dev = pick_device(args)
    with dev:
        cu(dev, CU_KEYS_SAVE)
    print("saved")


def cmd_keys_clear(args):
    if not args.yes:
        sys.exit("this zeroes all key counters; run again with --yes")
    dev = pick_device(args)
    with dev:
        cu(dev, CU_KEYS_CLEAR, struct.pack("<H", KEYS_CLEAR_MAGIC))
    print("key counters cleared")


# ---- per-device settings

PROFILE_FIELDS = ("mode", "mac", "winlock", "nkro", "frow", "bar", "level")


def unpack_profile(b):
    return {"valid": b[0] & 1, "mac": b[0] >> 1 & 1, "winlock": b[0] >> 2 & 1, "nkro": b[0] >> 3 & 1,
            "frow": b[0] >> 4 & 1, "mode": b[0] >> 5 & 3, "bar": b[1], "level": b[2] + 1}


def pack_profile(p):
    flags = 1 | p["mac"] << 1 | p["winlock"] << 2 | p["nkro"] << 3 | p["frow"] << 4 | p["mode"] << 5
    return bytes([flags, p["bar"], p["level"] - 1, 0])


def cmd_profile(args):
    dev = pick_device(args)
    with dev:
        if args.action in ("on", "off"):
            cu(dev, CU_PROFILE_ENABLE, bytes([args.action == "on"]))
            print(f"per-device settings {args.action}")
            return
        if args.action == "set":
            if not args.slot or args.slot not in [d.lower() for d in DEVS]:
                sys.exit(f"set needs a device: {', '.join(DEVS)}")
            slot = [d.lower() for d in DEVS].index(args.slot)
            p = unpack_profile(cu(dev, CU_PROFILE_GET, bytes([slot]))[3:7])
            for item in args.values:
                key, _, val = item.partition("=")
                if key not in PROFILE_FIELDS:
                    sys.exit(f"unknown field {key}; fields: {', '.join(PROFILE_FIELDS)}")
                p[key] = MODE_ARGS[val] if key == "mode" and val in MODE_ARGS else int(val)
            cu(dev, CU_PROFILE_SET, bytes([slot]) + pack_profile(p))
        print(f"{'device':6s} {'mode':11s} {'os':4s} {'winlock':8s} {'nkro':5s} {'number row':11s} {'bar mode':9s} brightness")
        for slot, name in enumerate(DEVS):
            q = cu(dev, CU_PROFILE_GET, bytes([slot]))
            p = unpack_profile(q[3:7])
            mark = "*" if q[1] == slot else " "
            if not p["valid"]:
                print(f"{name:5s}{mark} (not used yet)")
                continue
            mode = MODES[p["mode"]] if p["mode"] < len(MODES) else str(p["mode"])
            print(f"{name:5s}{mark} {mode:11s} {'Mac' if p['mac'] else 'Win':4s} {'on' if p['winlock'] else 'off':8s} {'on' if p['nkro'] else 'off':5s} "
                  f"{'F1-F12' if p['frow'] else 'digits':11s} {p['bar']:<9d} {p['level']}/5")
        print(f"per-device settings {'on' if q[2] else 'off'}; * = active")


def cmd_mode(args):
    """Through VIA's custom value command (id_cu_mode), so it works without the CU commands too."""
    dev = pick_device(args)
    with dev:
        if args.mode:
            reply = dev.request([VIA_CUSTOM_SET, 0, ID_CU_MODE, MODE_ARGS[args.mode]], args.timeout)
            if reply is None or reply[0] != VIA_CUSTOM_SET:
                sys.exit("this firmware has no modes")
        reply = dev.request([VIA_CUSTOM_GET, 0, ID_CU_MODE], args.timeout)
        if reply is None or reply[0] != VIA_CUSTOM_GET:
            sys.exit("this firmware has no modes")
    print(f"mode: {MODES[reply[3]] if reply[3] < len(MODES) else reply[3]}")


# ---- external flash

def cmd_flash_dump(args):
    dev = pick_device(args)
    with dev:
        info = read_info(dev)
        size = args.length or ((1 << info["flash_log2"]) if info["flash_log2"] else 0)
        if not size:
            sys.exit("no external flash reported")
        data = bytearray()
        t0 = time.monotonic()
        for addr in range(args.start, args.start + size, 24):
            p = cu(dev, CU_FLASH_READ, struct.pack("<I", addr))
            data += p[4:28]
            if len(data) % (24 * 2000) == 0:
                print(f"  {len(data) // 1024} KB...", file=sys.stderr)
        data = data[:size]
    with open(args.file, "wb") as f:
        f.write(data)
    print(f"{len(data)} bytes from 0x{args.start:06X} written to {args.file} in {time.monotonic() - t0:.0f} s")


def cmd_format(args):
    if not args.yes:
        sys.exit("this erases the log and key counter regions of the external flash "
                 "(back them up first with flash-dump); run again with --yes")
    dev = pick_device(args)
    with dev:
        cu(dev, CU_FLASH_FORMAT, struct.pack("<I", FORMAT_MAGIC), timeout=60000)
    print("formatted")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dev", dest="vidpid", help="VID:PID (hex) of the interface to use")
    ap.add_argument("--timeout", type=int, default=1000, help="reply timeout in ms")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="raw HID interfaces found").set_defaults(fn=cmd_list)
    p = sub.add_parser("ping", help="VIA round trips (works on any VIA firmware)")
    p.add_argument("-n", "--count", type=int, default=20)
    p.set_defaults(fn=cmd_ping)
    p = sub.add_parser("echo", help="CU round trips with a full payload")
    p.add_argument("-n", "--count", type=int, default=50)
    p.set_defaults(fn=cmd_echo)
    sub.add_parser("info", help="keyboard state").set_defaults(fn=cmd_info)
    sub.add_parser("time", help="log the computer's time").set_defaults(fn=cmd_time)
    sub.add_parser("diag", help="wireless module counters (raw HID over the air)").set_defaults(fn=cmd_diag)
    p = sub.add_parser("mode", help="show or set the mode: normal | eco | game")
    p.add_argument("mode", nargs="?", choices=list(MODE_ARGS))
    p.set_defaults(fn=cmd_mode)
    p = sub.add_parser("mark", help="log a MARK record (e.g. before a battery test)")
    p.add_argument("value", type=int)
    p.set_defaults(fn=cmd_mark)
    p = sub.add_parser("log", help="read the event log (cached, only new records are read)")
    p.add_argument("--all", action="store_true", help="print every record")
    p.add_argument("--tail", type=int, default=40, help="records to print (default 40)")
    p.add_argument("--csv", help="also write all records to this CSV file")
    p.add_argument("--no-time", action="store_true", help="don't log the computer's time first")
    p.set_defaults(fn=cmd_log)
    p = sub.add_parser("keys", help="key press counters / heat map")
    p.add_argument("--svg", help="write a heat map SVG")
    p.add_argument("--csv", help="write the counts as CSV")
    p.add_argument("--top", type=int, default=10)
    p.set_defaults(fn=cmd_keys)
    sub.add_parser("keys-save", help="save the counters to flash now").set_defaults(fn=cmd_keys_save)
    p = sub.add_parser("keys-clear", help="zero the counters")
    p.add_argument("--yes", action="store_true")
    p.set_defaults(fn=cmd_keys_clear)
    p = sub.add_parser("profile", help="per-device settings: show | on | off | set DEVICE field=value ...")
    p.add_argument("action", nargs="?", default="show", choices=["show", "on", "off", "set"])
    p.add_argument("slot", nargs="?", type=str.lower, help="device for set: usb bt1 bt2 bt3 bt4 bt5 2.4g")
    p.add_argument("values", nargs="*", help="mode=normal/eco/game mac=0/1 winlock=0/1 nkro=0/1 frow=0/1 bar=0-12 level=1-5")
    p.set_defaults(fn=cmd_profile)
    p = sub.add_parser("flash-dump", help="back up the external flash to a file")
    p.add_argument("file")
    p.add_argument("--start", type=lambda s: int(s, 0), default=0)
    p.add_argument("--length", type=lambda s: int(s, 0), default=0, help="default: whole chip")
    p.set_defaults(fn=cmd_flash_dump)
    p = sub.add_parser("format", help="erase the log / counter regions (needed if 'info' says LOCKED)")
    p.add_argument("--yes", action="store_true")
    p.set_defaults(fn=cmd_format)
    args = ap.parse_args()
    try:
        args.fn(args)
    except CuError as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
