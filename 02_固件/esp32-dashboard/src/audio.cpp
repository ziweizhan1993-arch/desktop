#include "audio.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

#include "config.h"

static constexpr int kRate = 22050;
static constexpr int kChunk = 256;

static QueueHandle_t s_queue;
static volatile uint8_t s_volume = 60;

struct Note {
  uint16_t freq;  // 0 = 静音
  uint16_t ms;
};

static const Note kBoot[] = {{784, 80}, {1047, 140}};
static const Note kClick[] = {{1500, 25}};
static const Note kStart[] = {{523, 110}, {659, 110}, {784, 110}, {1047, 280}};
static const Note kFinish[] = {{784, 130}, {988, 130}, {1175, 130}, {1568, 220},
                               {0, 60},    {1175, 120}, {1568, 420}};
static const Note kFail[] = {{659, 220}, {0, 60}, {523, 220}, {0, 60}, {392, 480}};

static void writeTone(uint16_t freq, uint16_t ms) {
  static int16_t buf[kChunk * 2];
  const int total = (int)((int32_t)kRate * ms / 1000);
  const int attack = kRate * 5 / 1000;
  const int release = min(total / 3, kRate * 40 / 1000);
  const float amp = 9000.0f * s_volume / 100.0f;
  const float step = 2.0f * PI * freq / kRate;
  float phase = 0;

  int i = 0;
  while (i < total) {
    const int n = min(kChunk, total - i);
    for (int k = 0; k < n; k++, i++) {
      float env = 1.0f;
      if (i < attack) env = (float)i / attack;
      else if (i > total - release) env = (float)(total - i) / release;
      float s = 0;
      if (freq) s = (sinf(phase) + 0.25f * sinf(2 * phase)) / 1.25f * amp * env;
      phase += step;
      if (phase > 2 * PI) phase -= 2 * PI;
      buf[2 * k] = buf[2 * k + 1] = (int16_t)s;
    }
    size_t written = 0;
    const esp_err_t err = i2s_write(I2S_NUM_0, buf, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    if (err != ESP_OK) Serial.printf("[audio] i2s_write error %d\n", err);
  }
}

template <size_t N>
static void playSeq(const Note (&notes)[N]) {
  for (const Note& n : notes) writeTone(n.freq, n.ms);
}

static void audioTask(void*) {
  Sound s;
  for (;;) {
    if (xQueueReceive(s_queue, &s, portMAX_DELAY) != pdTRUE) continue;
    if (s_volume == 0) continue;
    const uint32_t t0 = millis();
    switch (s) {
      case Sound::Boot: playSeq(kBoot); break;
      case Sound::Click: playSeq(kClick); break;
      case Sound::PrintStart: playSeq(kStart); break;
      case Sound::PrintFinish: playSeq(kFinish); break;
      case Sound::PrintFailed: playSeq(kFail); break;
    }
    writeTone(0, 40);
    i2s_zero_dma_buffer(I2S_NUM_0);
    Serial.printf("[audio] played sound %d in %lums, volume %d%%\n", (int)s,
                  (unsigned long)(millis() - t0), s_volume);
  }
}

void audioBegin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = kChunk;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  if (err != ESP_OK) Serial.printf("[audio] i2s_driver_install failed %d\n", err);

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRC;
  pins.data_out_num = PIN_I2S_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  err = i2s_set_pin(I2S_NUM_0, &pins);
  if (err != ESP_OK) Serial.printf("[audio] i2s_set_pin failed %d\n", err);
  i2s_zero_dma_buffer(I2S_NUM_0);

  s_queue = xQueueCreate(4, sizeof(Sound));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, nullptr, 0);
}

void audioPlay(Sound s) {
  if (s_queue) xQueueSend(s_queue, &s, 0);
}

void audioSetVolume(uint8_t percent) { s_volume = min<uint8_t>(percent, 100); }

uint8_t audioGetVolume() { return s_volume; }
