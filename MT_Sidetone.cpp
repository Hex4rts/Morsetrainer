#include "MT_Sidetone.h"
#include "MT_Pins.h"
#include <driver/i2s.h>
#include <math.h>

// ============================================================================
//  Internal state
// ============================================================================
static volatile bool     tone_on       = false;
static volatile uint16_t tone_freq     = SIDETONE_DEFAULT_FREQ;
static volatile uint8_t  tone_volume   = SIDETONE_DEFAULT_VOL;

// Phase accumulator (accessed only from the feed task)
static float phase      = 0.0f;
static float phase_inc  = 0.0f;

// Pre-computed sine table (256 entries, one full cycle)
#define SINE_TABLE_LEN 256
static int16_t sineTable[SINE_TABLE_LEN];

// DMA write buffer (stereo 16-bit → 4 bytes per frame)
#define FEED_FRAMES  256
static int16_t feedBuf[FEED_FRAMES * 2];  // L + R interleaved

static TaskHandle_t feedTaskHandle = NULL;

// ============================================================================
//  Helpers
// ============================================================================
static void recomputePhaseInc(void) {
  phase_inc = (float)tone_freq * SINE_TABLE_LEN / (float)SIDETONE_SAMPLE_RATE;
}

static void buildSineTable(void) {
  for (int i = 0; i < SINE_TABLE_LEN; i++) {
    sineTable[i] = (int16_t)(sinf(2.0f * M_PI * i / SINE_TABLE_LEN) * 32767.0f);
  }
}

// ============================================================================
//  Feed task — runs continuously, writes silence or tone to I2S DMA
// ============================================================================
static void sidetone_feed_task(void* arg) {
  size_t written;
  while (true) {
    if (tone_on) {
      float vol = tone_volume / 100.0f;
      for (int i = 0; i < FEED_FRAMES; i++) {
        // Linear interpolation in sine table
        int idx = (int)phase;
        float frac = phase - idx;
        idx &= (SINE_TABLE_LEN - 1);
        int next = (idx + 1) & (SINE_TABLE_LEN - 1);
        float sample = sineTable[idx] * (1.0f - frac) + sineTable[next] * frac;
        int16_t val = (int16_t)(sample * vol);
        feedBuf[i * 2]     = val;  // L
        feedBuf[i * 2 + 1] = val;  // R
        phase += phase_inc;
        if (phase >= SINE_TABLE_LEN) phase -= SINE_TABLE_LEN;
      }
    } else {
      memset(feedBuf, 0, sizeof(feedBuf));
      phase = 0.0f;
    }
    i2s_write(I2S_NUM_0, feedBuf, sizeof(feedBuf), &written, portMAX_DELAY);
  }
}

// ============================================================================
//  Public API
// ============================================================================
void Sidetone_Init(void) {
  buildSineTable();
  recomputePhaseInc();

  // Legacy I2S driver config (works on ESP32 Arduino core 2.x and 3.x)
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = SIDETONE_SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = 256,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
  };

  i2s_pin_config_t pin_config = {
    .mck_io_num   = I2S_PIN_NO_CHANGE,
    .bck_io_num   = I2S_BCLK_PIN,
    .ws_io_num    = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);

  // Feed task on core 0 (keeps audio glitch-free while LVGL runs on core 1)
  xTaskCreatePinnedToCore(sidetone_feed_task, "sidetone", 4096, NULL, 5, &feedTaskHandle, 0);
  printf("Sidetone: init OK  freq=%d vol=%d\n", tone_freq, tone_volume);
}

void Sidetone_On(void)  { tone_on = true; }
void Sidetone_Off(void) { tone_on = false; }
bool Sidetone_IsOn(void) { return tone_on; }

void Sidetone_SetFreq(uint16_t hz) {
  if (hz < 200)  hz = 200;
  if (hz > 1200) hz = 1200;
  tone_freq = hz;
  recomputePhaseInc();
}
uint16_t Sidetone_GetFreq(void) { return tone_freq; }

void Sidetone_SetVolume(uint8_t vol) {
  if (vol > 100) vol = 100;
  tone_volume = vol;
}
uint8_t Sidetone_GetVolume(void) { return tone_volume; }
