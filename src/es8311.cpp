// es8311.cpp -- 极简 ES8311 codec 驱动（播放通路）
//
// 寄存器序列严格照 Espressif 官方 esp_codec_dev/components/es8311/es8311.c：
//   ① 复位/上电  ② 时钟分频  ③ 音频格式  ④ 模拟电路上电
// 本板 I2S 由 ESP32-S3 做主（ES8311 为 slave），MCLK = 256 * fs。
//
// 注意：读-改-写寄存器（0x02 / 0x06 / 0x07）保留位依赖复位后的默认值，
// 所以必须先做 ① 复位，再改这些寄存器。

#include <Arduino.h>
#include <Wire.h>

#include "es8311.h"
#include "app_log.h"

#define ES8311_ADDR        0x18
#define I2C_SDA            4
#define I2C_SCL            5
#define I2C_FREQ           400000

// ---- 寄存器地址（只列用到的）----
#define REG_RESET_00       0x00
#define REG_CLK_01         0x01
#define REG_CLK_02         0x02
#define REG_CLK_03         0x03
#define REG_CLK_04         0x04
#define REG_CLK_05         0x05
#define REG_CLK_06         0x06
#define REG_CLK_07         0x07
#define REG_CLK_08         0x08
#define REG_SDPIN_09       0x09
#define REG_SDPOUT_0A      0x0A
#define REG_SYS_0D         0x0D
#define REG_SYS_0E         0x0E
#define REG_SYS_12         0x12
#define REG_SYS_13         0x13
#define REG_ADC_1C         0x1C
#define REG_DAC_31         0x31
#define REG_DAC_32         0x32
#define REG_DAC_37         0x37

static bool s_i2c_ready = false;

bool es8311_write_reg(uint8_t reg, uint8_t val)
{
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool es8311_read_reg(uint8_t reg, uint8_t *val)
{
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) {
    return false;
  }
  *val = Wire.read();
  return true;
}

// 读-改-写：只改 mask 对应的位
static void update_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
  uint8_t v = 0;
  if (!es8311_read_reg(reg, &v)) {
    return;
  }
  v = (uint8_t)((v & ~mask) | (value & mask));
  es8311_write_reg(reg, v);
}

void es8311_init(uint32_t sample_rate)
{
  if (!s_i2c_ready) {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_FREQ);
    s_i2c_ready = true;
  }

  // —— 探测器件是否在线 ——
  Wire.beginTransmission(ES8311_ADDR);
  const uint8_t rc = Wire.endTransmission();
  if (rc != 0) {
    LOG.printf("[es8311] NOT FOUND on I2C (err=%u), check SDA=4/SCL=5\n", rc);
    return;
  }
  LOG.println("[es8311] found on I2C 0x18");

  // —— ① 复位 / 上电 ——
  es8311_write_reg(REG_RESET_00, 0x1F);     // 软复位
  delay(20);
  es8311_write_reg(REG_RESET_00, 0x00);     // 退出复位
  es8311_write_reg(REG_RESET_00, 0x80);     // power on

  // —— ② 时钟：MCLK 取自 MCLK 引脚，全时钟使能 ——
  es8311_write_reg(REG_CLK_01, 0x3F);       // bit7=0 -> MCLK 来自 MCLK pin
  update_bits(REG_CLK_06, 0x20, 0x00);      // 清 bit5：SCLK 不反相

  // —— ② 分频系数：MCLK = 256*fs 对应一组固定值 ——
  //     pre_div=1, pre_multi=0, adc_div=1, dac_div=1, bclk_div=4
  update_bits(REG_CLK_02, 0xF8, (uint8_t)((1 - 1) << 5 | (0 << 3)));
  es8311_write_reg(REG_CLK_03, (uint8_t)((0 << 6) | 0x10));  // fs_mode=0, adc_osr=0x10
  es8311_write_reg(REG_CLK_04, 0x10);                        // dac_osr=0x10
  es8311_write_reg(REG_CLK_05, (uint8_t)(((1 - 1) << 4) | (1 - 1)));
  update_bits(REG_CLK_06, 0x1F, (uint8_t)(4 - 1));            // bclk_div=4
  update_bits(REG_CLK_07, 0x3F, 0x00);                        // lrck_h
  es8311_write_reg(REG_CLK_08, 0xFF);                         // lrck_l

  // —— ③ 格式：slave + I2S Philips + 16bit ——
  update_bits(REG_RESET_00, 0x40, 0x00);    // 清 bit6 -> slave 模式
  es8311_write_reg(REG_SDPIN_09,  0x0C);    // 16bit: 3<<2
  es8311_write_reg(REG_SDPOUT_0A, 0x0C);

  // —— ④ 模拟电路上电 ——
  es8311_write_reg(REG_SYS_0D, 0x01);       // 模拟电路上电
  es8311_write_reg(REG_SYS_0E, 0x02);       // 使能 PGA + ADC 调制器
  es8311_write_reg(REG_SYS_12, 0x00);       // DAC 上电
  es8311_write_reg(REG_SYS_13, 0x10);       // 使能 HP 输出
  es8311_write_reg(REG_ADC_1C, 0x6A);       // ADC EQ 旁路 + 去直流
  es8311_write_reg(REG_DAC_37, 0x08);       // DAC EQ 旁路

  es8311_set_volume(70);
  es8311_mute(false);

  LOG.printf("[es8311] init done, fs=%lu Hz (MCLK=%lu)\n",
             (unsigned long)sample_rate, (unsigned long)(sample_rate * 256));
}

void es8311_set_volume(uint8_t vol)
{
  if (vol > 100) vol = 100;
  const uint8_t reg = (vol == 0) ? 0
                    : (uint8_t)((vol * 256 / 100) - 1);
  es8311_write_reg(REG_DAC_32, reg);
}

void es8311_mute(bool mute)
{
  update_bits(REG_DAC_31, 0x60, mute ? 0x60 : 0x00);
}
