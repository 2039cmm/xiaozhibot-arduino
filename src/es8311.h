// es8311.h -- 极简 ES8311 codec 驱动（只做播放通路）
//
// SparkBot 音频板：ES8311 走 I2C 控制（SDA=4 SCL=5，地址 0x18），
// I2S 出声（MCLK=45 BCLK=39 LRCK=41 DOUT=42 DIN=40）。
// 本驱动只实现「初始化 + 音量 + 静音」，够 SPK 放提示音/语音提示用。
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 I2C 并配置 ES8311。须在 I2S 已经输出 MCLK 之后调用
// （ES8311 的时钟状态机要看到 MCLK 才能锁定时钟分频）。
// sample_rate 必须满足 MCLK = 256 * sample_rate。
void es8311_init(uint32_t sample_rate);

// 音量 0~100（0 静音，100 = 0dB）
void es8311_set_volume(uint8_t vol);

// 静音 / 取消静音
void es8311_mute(bool mute);

// 直接读写寄存器（调试用）
bool es8311_write_reg(uint8_t reg, uint8_t val);
bool es8311_read_reg(uint8_t reg, uint8_t *val);

#ifdef __cplusplus
}
#endif
