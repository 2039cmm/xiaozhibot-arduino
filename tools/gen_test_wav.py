#!/usr/bin/env python3
# gen_test_wav.py -- 生成一段测试旋律 WAV 并转成 C 数组
#
# 输出：lib/audio/test_tone_wav.c （const uint8_t 数组）
# 规格：16000Hz / 单声道 / 16bit PCM —— 与 app_audio 的 I2S 配置一致
import math
import struct
import os

SR = 16000
DUR_PER_NOTE = 0.28
FADE = 0.02

# C5 E5 G5 C6 —— 上行琶音，好听且容易听出问题
NOTES = [523.25, 659.25, 783.99, 1046.50]


def gen_pcm():
    frames = []
    for freq in NOTES:
        n = int(SR * DUR_PER_NOTE)
        fade_n = int(SR * FADE)
        for i in range(n):
            env = 1.0
            if i < fade_n:
                env = i / fade_n
            elif i > n - fade_n:
                env = (n - i) / fade_n
            # 基频 + 一点二次谐波，听起来不那么干
            s = 0.6 * math.sin(2 * math.pi * freq * i / SR)
            s += 0.2 * math.sin(2 * math.pi * freq * 2 * i / SR)
            s *= env * 0.55
            v = max(-32767, min(32767, int(s * 32767)))
            frames.append(v)
    return frames


def build_wav(pcm):
    data = b"".join(struct.pack("<h", v) for v in pcm)
    n = len(pcm)
    byte_rate = SR * 1 * 2
    block_align = 2
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, SR, byte_rate, block_align, 16)
    hdr += b"data" + struct.pack("<I", len(data))
    return hdr + data


def to_c_array(blob, out_path):
    lines = []
    lines.append("// 自动生成，勿手改 —— 由 tools/gen_test_wav.py 生成")
    lines.append("// 规格: 16000Hz / 单声道 / 16bit PCM")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("const uint32_t g_test_wav_len = %d;" % len(blob))
    lines.append("const uint8_t g_test_wav[] = {")
    for i in range(0, len(blob), 16):
        chunk = blob[i:i + 16]
        lines.append("  " + ",".join("0x%02X" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return len(blob)


if __name__ == "__main__":
    pcm = gen_pcm()
    wav = build_wav(pcm)
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_dir = os.path.join(root, "src")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "test_tone_wav.c")

    # 同时存一份 wav 便于用播放器试听
    with open(os.path.join(root, "tools", "test_tone.wav"), "wb") as f:
        f.write(wav)

    size = to_c_array(wav, out_path)
    print("samples=%d  wav_bytes=%d" % (len(pcm), size))
    print("written:", out_path)
    print("duration=%.2fs" % (len(pcm) / SR))
