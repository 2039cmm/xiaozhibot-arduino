// app_audio.cpp -- ESP32-S3 + ES8311 音频输出
//
// 链路：ESP32-S3 I2S(主) --BCLK/LRCK/MCLK--> ES8311(slave, I2C 配置) -> HP -> 喇叭
//
// 引脚（官方 BSP 一致）：
//   MCLK=45  BCLK=39  LRCK=41  DOUT=42  DIN=40   I2C: SDA=4 SCL=5
//
// 注意：
//   1) 必须「先启动 I2S」让它输出 MCLK，再配置 ES8311 的时钟寄存器，
//      否则 ES8311 时钟状态机锁不住分频。
//   2) 本工程框架是 IDF 4.4，只能用传统的 driver/i2s.h（无 i2s_std.h）。

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "driver/i2s.h"

#include "app_audio.h"
#include "app_log.h"
#include "es8311.h"

#define I2S_PORT          I2S_NUM_0
#define AUDIO_FS          16000            // 与官方 CODEC_DEFAULT_SAMPLE_RATE 一致
#define I2S_MCLK_PIN      45
#define I2S_BCLK_PIN      39
#define I2S_LRCK_PIN      41
#define I2S_DOUT_PIN      42
#define I2S_DIN_PIN       40

static bool     s_ready = false;
static uint32_t s_cur_fs = AUDIO_FS;     // 当前 I2S 运行采样率

static void i2s_hw_init(void)
{
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate          = AUDIO_FS;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;   // 左右两声道
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;    // Philips
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = 6;
  cfg.dma_buf_len          = 240;
  cfg.use_apll             = false;
  cfg.tx_desc_auto_clear   = true;                          // 欠载时自动补静音
  cfg.fixed_mclk           = 0;
  cfg.mclk_multiple        = I2S_MCLK_MULTIPLE_256;         // MCLK = 256*fs

  esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    LOG.printf("[audio] i2s_driver_install failed: %d\n", (int)err);
    return;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_MCLK_PIN;
  pins.bck_io_num   = I2S_BCLK_PIN;
  pins.ws_io_num    = I2S_LRCK_PIN;
  pins.data_out_num = I2S_DOUT_PIN;
  pins.data_in_num  = I2S_DIN_PIN;

  err = i2s_set_pin(I2S_PORT, &pins);
  if (err != ESP_OK) {
    LOG.printf("[audio] i2s_set_pin failed: %d\n", (int)err);
    return;
  }
  LOG.println("[audio] i2s init ok (master, 16k, 16bit, stereo, mclk=256fs)");
}

void app_audio_init(void)
{
  if (s_ready) {
    return;
  }
  // 先让 I2S 跑起来输出 MCLK，再配置 codec
  i2s_hw_init();
  delay(60);
  es8311_init(AUDIO_FS);
  s_ready = true;
  LOG.println("[audio] ready");
}

bool app_audio_ready(void)
{
  return s_ready;
}

void app_audio_set_volume(uint8_t vol)
{
  es8311_set_volume(vol);
}

// 生成并播放一段正弦音。带 5ms 淡入淡出，避免爆音。
void app_audio_tone(uint32_t freq_hz, uint32_t duration_ms)
{
  if (!s_ready) {
    app_audio_init();
  }

  const int   rate     = AUDIO_FS;
  const int   total    = (int)((uint64_t)rate * duration_ms / 1000);
  const int   chunk    = 128;
  const float amp      = 0.35f * 32767.0f;      // 留足余量防削顶
  const float dphase   = 2.0f * (float)M_PI * (float)freq_hz / (float)rate;
  const int   fade_len = rate / 200;            // 5ms

  static int16_t buf[128 * 2];
  float phase = 0.0f;
  int   done  = 0;

  while (done < total) {
    int n = total - done;
    if (n > chunk) n = chunk;

    for (int i = 0; i < n; i++) {
      const int idx = done + i;
      float env = 1.0f;
      if (idx < fade_len) {
        env = (float)idx / (float)fade_len;
      } else if (idx > total - fade_len) {
        env = (float)(total - idx) / (float)fade_len;
      }

      const int16_t v = (int16_t)(sinf(phase) * amp * env);
      phase += dphase;
      if (phase > 2.0f * (float)M_PI) {
        phase -= 2.0f * (float)M_PI;
      }
      buf[i * 2]     = v;
      buf[i * 2 + 1] = v;
    }

    size_t written = 0;
    i2s_write(I2S_PORT, buf, (size_t)(n * 2 * sizeof(int16_t)),
              &written, portMAX_DELAY);
    done += n;
  }
}

void app_audio_beep(void)
{
  app_audio_tone(880, 90);
  delay(40);
  app_audio_tone(1320, 120);
}

// ============================ WAV 播放 ============================

uint32_t app_audio_sample_rate(void)
{
  return s_cur_fs;
}

// 从字节流里按小端读 16bit（避免直接强转未对齐指针）
static inline int16_t rd_s16(const uint8_t *p)
{
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t rd_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct {
  const uint8_t *pcm;
  uint32_t pcm_bytes;
  uint16_t channels;
  uint16_t bits;
  uint32_t rate;
} wav_info_t;

// 解析 RIFF/WAVE，找到 fmt 与 data 块
static bool wav_parse(const uint8_t *wav, uint32_t len, wav_info_t *out)
{
  if (len < 44) return false;
  if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
    return false;
  }

  uint16_t channels = 0, bits = 0;
  uint32_t rate = 0;

  uint32_t p = 12;
  while (p + 8 <= len) {
    const uint8_t *id  = wav + p;
    const uint32_t sz  = rd_u32(wav + p + 4);
    const uint8_t *pay = wav + p + 8;

    if (memcmp(id, "fmt ", 4) == 0 && sz >= 16 && p + 8 + 16 <= len) {
      const uint16_t audio_fmt = (uint16_t)(pay[0] | (pay[1] << 8));
      if (audio_fmt != 1) {                 // 只支持 PCM
        LOG.printf("[audio] wav fmt=%u not PCM\n", audio_fmt);
        return false;
      }
      channels = (uint16_t)(pay[2] | (pay[3] << 8));
      rate     = rd_u32(pay + 4);
      bits     = (uint16_t)(pay[14] | (pay[15] << 8));
    } else if (memcmp(id, "data", 4) == 0) {
      out->pcm       = pay;
      out->pcm_bytes = (sz <= len - (p + 8)) ? sz : (len - (p + 8));
      out->channels  = channels;
      out->bits      = bits;
      out->rate      = rate;
      return (channels >= 1 && bits == 16 && out->pcm_bytes > 0);
    }

    p += 8 + sz + (sz & 1);                  // 块按偶数字节对齐
  }
  return false;
}

// 切换采样率：重配 I2S 时钟 + ES8311 分频
static void set_sample_rate(uint32_t rate)
{
  if (rate == s_cur_fs) return;
  i2s_set_clk(I2S_PORT, rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  delay(20);
  es8311_init(rate);
  s_cur_fs = rate;
  LOG.printf("[audio] sample rate -> %lu\n", (unsigned long)rate);
}

bool app_audio_play_wav(const uint8_t *wav, uint32_t len)
{
  if (!s_ready) {
    app_audio_init();
  }

  wav_info_t info = {};
  if (!wav_parse(wav, len, &info)) {
    LOG.println("[audio] wav parse failed");
    return false;
  }
  LOG.printf("[audio] wav: %lu Hz, %u ch, %u bit, %lu bytes\n",
             (unsigned long)info.rate, info.channels, info.bits,
             (unsigned long)info.pcm_bytes);

  set_sample_rate(info.rate);

  const uint32_t frame_bytes = (uint32_t)info.channels * 2;   // 16bit
  const uint32_t frames      = info.pcm_bytes / frame_bytes;
  const uint32_t chunk       = 128;

  static int16_t out[128 * 2];
  uint32_t i = 0;
  while (i < frames) {
    uint32_t n = frames - i;
    if (n > chunk) n = chunk;

    for (uint32_t k = 0; k < n; k++) {
      const uint8_t *f = info.pcm + (i + k) * frame_bytes;
      if (info.channels == 1) {
        const int16_t s = rd_s16(f);
        out[k * 2]     = s;          // 单声道复制到左右
        out[k * 2 + 1] = s;
      } else {
        out[k * 2]     = rd_s16(f);          // 左
        out[k * 2 + 1] = rd_s16(f + 2);      // 右
      }
    }

    size_t written = 0;
    i2s_write(I2S_PORT, out, (size_t)(n * 2 * sizeof(int16_t)),
              &written, portMAX_DELAY);
    i += n;
  }

  // 尾巴补一小段静音，避免最后一帧被截断产生"咔"声
  memset(out, 0, sizeof(out));
  size_t written = 0;
  i2s_write(I2S_PORT, out, sizeof(out), &written, portMAX_DELAY);
  return true;
}
