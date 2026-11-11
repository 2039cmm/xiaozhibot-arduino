#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从官方 esp_sparkbot 的 weather/*.png 生成 LVGL 天气图标 C 数组。

用法:
  python tools/gen_weather_icons.py --sheet
      生成 61 张官方图标的预览拼图（带编号），用来核对图标语义。
  python tools/gen_weather_icons.py
      按 DEFAULT_CODES 生成 lib/ui/images/ui_img_weather_icons.c
  python tools/gen_weather_icons.py 100 101 104 ...
      生成指定和风天气代码的图标。

纯标准库实现，不需要 Pillow。官方 PNG 为 110x110 / RGBA8 / 非隔行。

【字节序陷阱】
本工程 lv_conf.h 里 LV_COLOR_16_SWAP=0，lv_color_t.full 是小端 uint16，
所以 TRUE_COLOR_ALPHA 每像素 3 字节必须写成:
    [rgb565 低字节, rgb565 高字节, alpha]
而官方 SquareLine 导出的 ui_img_clody_png.c 用的是大端（高字节在前），
直接搬过来会整体串色（橙太阳变紫、白云变绿）。这里从原始 PNG 重算，
不复用官方导出的 C 数组。
"""

import glob
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
SRC_DIR = os.path.join(
    os.path.dirname(PROJECT), "esp_sparkbot", "example",
    "factory_demo_v1", "weather")
OUT_C = os.path.join(PROJECT, "lib", "ui", "images",
                     "ui_img_weather_icons.c")
SHEET = os.path.join(os.path.dirname(PROJECT), "tmp_weather_sheet.png")

# 覆盖 Open-Meteo WMO code 所需的代表性图标（和风天气代码）
DEFAULT_CODES = [
    "100",  # 晴           WMO 0（纯太阳）
    "103",  # 晴间多云      WMO 1 少云（大太阳 + 一层薄云）
    "104",  # 多云          WMO 2（太阳被云遮住大半）
    "102",  # 阴            WMO 3（纯厚云，无太阳）
    "501",  # 雾            WMO 45/48
    "300",  # 阵雨          WMO 80-82
    "302",  # 雷阵雨        WMO 95
    "304",  # 雷阵雨伴冰雹  WMO 96/99
    "305",  # 小雨          WMO 51-55/61
    "306",  # 中雨          WMO 63
    "307",  # 大雨          WMO 65
    "313",  # 冻雨          WMO 56/57/66/67
    "400",  # 小雪          WMO 71/77/85/86
    "401",  # 中雪          WMO 73
    "402",  # 大雪          WMO 75
    "999",  # 未知 / 未就绪
]

DIGITS = {
    "0": ["111", "101", "101", "101", "111"],
    "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"],
    "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"],
    "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"],
    "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"],
    "9": ["111", "101", "111", "001", "111"],
}


def read_png_rgba(path):
    """解码非隔行 RGBA8 PNG，返回 (w, h, bytes)。"""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: " + path)

    pos = 8
    idat = bytearray()
    w = h = bd = ct = inter = None
    while pos + 12 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w, h, bd, ct, comp, filt, inter = struct.unpack(
                ">IIBBBBB", chunk)
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break

    if bd != 8 or ct != 6 or inter != 0:
        raise ValueError("need RGBA8 non-interlaced, got %r" %
                         ((bd, ct, inter),))

    raw = zlib.decompress(bytes(idat))
    stride = w * 4
    prev = bytearray(stride)
    out = bytearray(h * stride)
    p = 0
    for y in range(h):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:                       # Sub
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 255
        elif f == 2:                     # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:                     # Average
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:                     # Paeth
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (
                    b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        elif f != 0:
            raise ValueError("bad PNG filter %d" % f)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bytes(out)


def write_png_rgba(path, w, h, rgba):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    open(path, "wb").write(png)


def to_lvgl(w, h, rgba):
    """RGBA8 -> LV_IMG_CF_TRUE_COLOR_ALPHA (小端 RGB565 + alpha)。"""
    out = bytearray(w * h * 3)
    for i in range(w * h):
        r, g, b, a = rgba[i * 4:i * 4 + 4]
        c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[i * 3 + 0] = c & 0xFF
        out[i * 3 + 1] = c >> 8
        out[i * 3 + 2] = a
    return bytes(out)


def all_pngs():
    return sorted(glob.glob(os.path.join(SRC_DIR, "*.png")),
                  key=lambda p: os.path.basename(p))


def make_sheet():
    """把 61 张图标按文件名顺序排成 8 列，每格 60px，底部画 3 位编号。"""
    files = all_pngs()
    cell, cols = 60, 8
    rows = (len(files) + cols - 1) // cols
    W, H = cols * cell, rows * cell
    buf = bytearray(W * H * 4)
    for i in range(W * H):              # 深灰底
        buf[i * 4:i * 4 + 4] = bytes((32, 34, 38, 255))

    def put(x, y, r, g, b):
        if 0 <= x < W and 0 <= y < H:
            j = (y * W + x) * 4
            buf[j:j + 4] = bytes((r, g, b, 255))

    for idx, path in enumerate(files):
        code = os.path.splitext(os.path.basename(path))[0]
        w, h, rgba = read_png_rgba(path)
        ox = (idx % cols) * cell + (cell - w // 2) // 2
        oy = (idx // cols) * cell + 1
        # 2x 缩小：110 -> 55
        for y in range(0, h, 2):
            for x in range(0, w, 2):
                r, g, b, a = rgba[(y * w + x) * 4:(y * w + x) * 4 + 4]
                if a < 16:
                    continue
                for dy in (0,):
                    put(ox + x // 2, oy + y // 2 + dy, r, g, b)
        # 3 位编号，2x 点阵，画在格子底部
        bx = (idx % cols) * cell + 4
        by = (idx // cols) * cell + cell - 11
        for ci, ch in enumerate(code.rjust(3, "0")):
            pat = DIGITS[ch]
            for ry, row in enumerate(pat):
                for rx, bit in enumerate(row):
                    if bit == "1":
                        px = bx + ci * 9 + rx * 2
                        py = by + ry * 2
                        for dy in (0, 1):
                            for dx in (0, 1):
                                put(px + dx, py + dy, 240, 200, 60)
    write_png_rgba(SHEET, W, H, bytes(buf))
    print("sheet:", SHEET, W, "x", H, "icons:", len(files))
    print("order:", " ".join(os.path.splitext(os.path.basename(p))[0]
                             for p in files))


def make_c(codes):
    parts = [
        "// 自动生成，请勿手改。来源: esp_sparkbot/example/factory_demo_v1/weather/*.png",
        "// 生成工具: tools/gen_weather_icons.py",
        "// 格式: LV_IMG_CF_TRUE_COLOR_ALPHA, 110x110, 小端 RGB565 + alpha",
        "// 本工程 LV_COLOR_16_SWAP=0；不要直接搬官方 SquareLine 导出的 C 数组（那是大端）。",
        "",
        '#include "../ui.h"',
        "",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
        "#define LV_ATTRIBUTE_MEM_ALIGN",
        "#endif",
        "#ifndef LV_ATTRIBUTE_LARGE_CONST",
        "#define LV_ATTRIBUTE_LARGE_CONST",
        "#endif",
        "",
    ]
    total = 0
    for code in codes:
        path = os.path.join(SRC_DIR, code + ".png")
        if not os.path.isfile(path):
            raise SystemExit("missing icon: " + path)
        w, h, rgba = read_png_rgba(path)
        px = to_lvgl(w, h, rgba)
        total += len(px)
        name = "ui_img_wx_" + code
        parts.append("const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST "
                     "uint8_t %s_data[] = {" % name)
        for off in range(0, len(px), 24):
            parts.append("  " + "".join("0x%02X," % b
                                        for b in px[off:off + 24]))
        parts.append("};")
        parts.append("const lv_img_dsc_t %s = {" % name)
        parts.append("  .header.always_zero = 0,")
        parts.append("  .header.w = %d," % w)
        parts.append("  .header.h = %d," % h)
        parts.append("  .data_size = sizeof(%s_data)," % name)
        parts.append("  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
        parts.append("  .data = %s_data" % name)
        parts.append("};")
        parts.append("")
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts))
    print("wrote:", OUT_C)
    print("icons:", len(codes), "pixel bytes:", total,
          "file bytes:", os.path.getsize(OUT_C))
    print("LV_IMG_DECLARE lines:")
    for code in codes:
        print("  LV_IMG_DECLARE(ui_img_wx_%s);" % code)


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--sheet":
        make_sheet()
    elif args:
        make_c(args)
    else:
        make_c(DEFAULT_CODES)
