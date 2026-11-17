// app_audio.h -- 音频输出（ES8311 + I2S）
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 I2S（主模式，MCLK = 256*fs）并配置 ES8311。可重复调用（幂等）。
void app_audio_init(void);

// 播放一段正弦提示音
void app_audio_tone(uint32_t freq_hz, uint32_t duration_ms);

// 播放一段内存里的 WAV（支持 16bit PCM，单声道/立体声）。
// 采样率与当前不一致时会重配 I2S + ES8311。返回是否成功播放。
bool app_audio_play_wav(const uint8_t *wav, uint32_t len);

// 当前采样率
uint32_t app_audio_sample_rate(void);

// 开机/按键提示音（短促双音）
void app_audio_beep(void);

// 音量 0~100
void app_audio_set_volume(uint8_t vol);

// 当前状态
bool app_audio_ready(void);

#ifdef __cplusplus
}
#endif
