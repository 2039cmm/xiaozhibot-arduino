#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""反解 ui_img_weather_icons.c，验证字节序并生成实机黑底预览图。

为什么需要这一步：
  本工程 LV_COLOR_16_SWAP=0，TRUE_COLOR_ALPHA 每像素 3 字节必须是
  [rgb565 低字节, rgb565 高字节, alpha]。官方 SquareLine 导出的是大端，
  一旦搞反，太阳会变紫、白云会变绿，而上板后才能用肉眼发现。

做法：
  1. 从生成的 C 文件里把每个数组抠出来；
  2. 分别按小端 / 大端还原成 RGB565；
  3. 与「源 PNG 经过同样 RGB565 量化」的参考图逐像素比较。
     字节序正确时误差必须为 0（因为数据就是这么生成的），
     字节序错误时误差会非常大；
  4. 按小端还原结果合成到黑底（模拟实机 LCD），输出 4x4 预览拼图。

用法: python tools/verify_weather_icons.py
输出: ../tmp_weather_icons_verify.png
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_weather_icons as g  # noqa: E402  复用 PNG 编解码与点阵数字

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
C_PATH = os.path.join(PROJECT, "lib", "ui", "images",
                      "ui_img_weather_icons.c")
OUT_PNG = os.path.join(os.path.dirname(PROJECT),
                       "tmp_weather_icons_verify.png")

W = H = 110
PX_BYTES = W * H * 3          # 36300


def parse_c():
    """返回 [(code, bytes)]，顺序与 C 文件一致。"""
    text = open(C_PATH, "r", encoding="utf-8").read()
    pat = re.compile(
        r"uint8_t\s+ui_img_wx_(\d+)_data\[\]\s*=\s*\{(.*?)\};", re.S)
    out = []
    for m in pat.finditer(text):
        code = m.group(1)
        raw = bytes(int(x, 16) for x in
                    re.findall(r"0x([0-9A-Fa-f]{2})", m.group(2)))
        out.append((code, raw))
    return out


def decode_rgba(px, big_endian=False):
    """RGB565A8 -> RGBA8。"""
    out = bytearray(W * H * 4)
    for i in range(W * H):
        b0, b1, a = px[i * 3], px[i * 3 + 1], px[i * 3 + 2]
        c = ((b0 << 8) | b1) if big_endian else (b0 | (b1 << 8))
        r = ((c >> 11) & 31) * 255 // 31
        gg = ((c >> 5) & 63) * 255 // 63
        bb = (c & 31) * 255 // 31
        out[i * 4:i * 4 + 4] = bytes((r, gg, bb, a))
    return bytes(out)


def quantize_ref(rgba):
    """源 PNG 也过一遍 RGB565 量化，让判据只针对字节序，不含量化噪声。"""
    out = bytearray(len(rgba))
    for i in range(W * H):
        r, gg, b, a = rgba[i * 4:i * 4 + 4]
        c = ((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3)
        out[i * 4 + 0] = ((c >> 11) & 31) * 255 // 31
        out[i * 4 + 1] = ((c >> 5) & 63) * 255 // 63
        out[i * 4 + 2] = (c & 31) * 255 // 31
        out[i * 4 + 3] = a
    return bytes(out)


def diff(a, b):
    """返回 (不同像素数, 平均通道绝对误差)，只统计非透明像素。"""
    bad = 0
    total = 0
    n = 0
    for i in range(W * H):
        if a[i * 4 + 3] < 16:
            continue
        d = (abs(a[i * 4] - b[i * 4]) +
             abs(a[i * 4 + 1] - b[i * 4 + 1]) +
             abs(a[i * 4 + 2] - b[i * 4 + 2]))
        if d:
            bad += 1
        total += d
        n += 1
    return bad, (total / (n * 3) if n else 0.0)


def make_preview(icons):
    """合成到黑底，模拟实机显示；格子底部画 3 位和风代码。"""
    cols = 4
    label_h = 16
    rows = (len(icons) + cols - 1) // cols
    WW, HH = cols * W, rows * (H + label_h)
    buf = bytearray(WW * HH * 4)
    for i in range(WW * HH):
        buf[i * 4:i * 4 + 4] = bytes((0, 0, 0, 255))

    def put(x, y, r, gg, b):
        if 0 <= x < WW and 0 <= y < HH:
            j = (y * WW + x) * 4
            buf[j:j + 4] = bytes((r, gg, b, 255))

    for idx, (code, rgba) in enumerate(icons):
        ox = (idx % cols) * W
        oy = (idx // cols) * (H + label_h)
        for y in range(H):
            for x in range(W):
                r, gg, b, a = rgba[(y * W + x) * 4:(y * W + x) * 4 + 4]
                if a == 0:
                    continue
                j = ((oy + y) * WW + ox + x) * 4
                inv = 255 - a
                buf[j] = (buf[j] * inv + r * a) // 255
                buf[j + 1] = (buf[j + 1] * inv + gg * a) // 255
                buf[j + 2] = (buf[j + 2] * inv + b * a) // 255
        # 编号：3 位数字，2x 点阵，橙黄色
        bx = ox + 4
        by = oy + H + 1
        for ci, ch in enumerate(code.rjust(3, "0")):
            for ry, row in enumerate(g.DIGITS[ch]):
                for rx, bit in enumerate(row):
                    if bit == "1":
                        px, py = bx + ci * 9 + rx * 2, by + ry * 2
                        for dy in (0, 1):
                            for dx in (0, 1):
                                put(px + dx, py + dy, 240, 190, 60)

    g.write_png_rgba(OUT_PNG, WW, HH, bytes(buf))
    return WW, HH


def main():
    parsed = parse_c()
    if not parsed:
        raise SystemExit("no icon arrays found in " + C_PATH)

    print("file:", C_PATH)
    print("icons:", len(parsed))
    print()
    print("%-6s %-8s %-14s %-14s %s" %
          ("code", "bytes", "LE diff px/err", "BE diff px/err", "verdict"))
    print("-" * 62)

    decoded = []
    all_ok = True
    for code, px in parsed:
        size_ok = (len(px) == PX_BYTES)
        le = decode_rgba(px, big_endian=False)
        be = decode_rgba(px, big_endian=True)

        src = os.path.join(g.SRC_DIR, code + ".png")
        if os.path.isfile(src):
            sw, sh, srgba = g.read_png_rgba(src)
            ref = quantize_ref(srgba)
            le_bad, le_err = diff(le, ref)
            be_bad, be_err = diff(be, ref)
            ok = size_ok and le_bad == 0 and be_bad > 0
            ds = "%d / %.1f" % (le_bad, le_err)
            bs = "%d / %.1f" % (be_bad, be_err)
        else:
            ok = size_ok
            ds = bs = "(no src)"

        all_ok &= bool(ok)
        verdict = "LE OK" if ok else "CHECK!"
        print("%-6s %-8d %-14s %-14s %s" %
              (code, len(px), ds, bs, verdict))
        decoded.append((code, le))

    ww, hh = make_preview(decoded)
    print()
    print("preview:", OUT_PNG, "%dx%d" % (ww, hh))
    print("total pixel bytes:", sum(len(p) for _, p in parsed))
    print()
    print("RESULT:", "PASS - 小端字节序正确，图标与官方源图逐像素一致"
          if all_ok else "FAIL - 有图标不一致，请检查生成脚本")


if __name__ == "__main__":
    main()
