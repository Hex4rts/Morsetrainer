#include "MT_Keyer.h"
#include "MT_Pins.h"
#include "MT_Sidetone.h"
#include "MT_Morse.h"
#include <esp_timer.h>

// ============================================================================
//  Internal state
// ============================================================================
typedef enum {
  KS_IDLE,        // waiting for paddle
  KS_DIT,         // dit sounding
  KS_DAH,         // dah sounding
  KS_IEG,         // inter-element gap (1 dit)
  KS_CHAR_WAIT,   // waiting to see if more elements come (2 extra dit times)
} keyer_state_t;

static keyer_mode_t  mode       = KEYER_IAMBIC_B;
static uint8_t       wpm        = 20;
static bool          swapped    = false;

static keyer_state_t state      = KS_IDLE;
static uint16_t      timer_ms   = 0;      // countdown for current state
static uint16_t      dit_ms     = 60;     // recalculated from WPM
static bool          sending    = false;   // true while key is down

// Word space detection
static uint16_t      idle_ms    = 0;       // time spent idle after last char
static bool          space_sent = true;    // prevent multiple spaces

// Squeeze latch (for iambic)
static bool dit_latch  = false;
static bool dah_latch  = false;

// Pattern accumulator for decoding
#define PATTERN_MAX 8
static char  pattern[PATTERN_MAX + 1];
static uint8_t pat_len = 0;

// Callbacks
static keyer_char_cb_t    char_cb    = NULL;
static keyer_element_cb_t element_cb = NULL;

// Timer handle
static esp_timer_handle_t keyer_timer = NULL;

// ============================================================================
//  Helpers
// ============================================================================
static void recalcTiming(void) {
  dit_ms = 1200 / wpm;
}

// Debounce state (5ms = 5 consecutive 1ms ticks)
#define DEBOUNCE_MS 5
static uint8_t dit_stable = 0;
static uint8_t dah_stable = 0;
static bool    dit_state  = false;
static bool    dah_state  = false;

static bool readDit(void) {
  bool raw = !digitalRead(swapped ? DAH_PADDLE_PIN : DIT_PADDLE_PIN);
  if (raw == dit_state) {
    dit_stable = DEBOUNCE_MS;  // reset counter while stable
  } else {
    if (dit_stable > 0) dit_stable--;
    if (dit_stable == 0) {
      dit_state = raw;         // accept new state after debounce
      dit_stable = DEBOUNCE_MS;
    }
  }
  return dit_state;
}

static bool readDah(void) {
  bool raw = !digitalRead(swapped ? DIT_PADDLE_PIN : DAH_PADDLE_PIN);
  if (raw == dah_state) {
    dah_stable = DEBOUNCE_MS;
  } else {
    if (dah_stable > 0) dah_stable--;
    if (dah_stable == 0) {
      dah_state = raw;
      dah_stable = DEBOUNCE_MS;
    }
  }
  return dah_state;
}

static void keyDown(bool isDah) {
  sending = true;
  Sidetone_On();
  if (element_cb) element_cb(true, isDah);
}

static void keyUp(void) {
  sending = false;
  Sidetone_Off();
  if (element_cb) element_cb(false, false);
}

// Display-copy of pattern (persists after emitChar for UI to read)
static char displayPattern[PATTERN_MAX + 1] = {};
static uint32_t displayPatternTime = 0;

static void patternReset(void) {
  pat_len = 0;
  pattern[0] = '\0';
}

static void patternAdd(char elem) {
  if (pat_len < PATTERN_MAX) {
    pattern[pat_len++] = elem;
    pattern[pat_len] = '\0';
    // Update display copy immediately
    memcpy(displayPattern, pattern, pat_len + 1);
    displayPatternTime = millis();
  }
}

static void emitChar(void) {
  if (pat_len == 0) return;
  // Save pattern for display before clearing
  memcpy(displayPattern, pattern, pat_len + 1);
  displayPatternTime = millis();
  char c = Morse_Decode(pattern);
  if (c && char_cb) char_cb(c);
  patternReset();
}

// ============================================================================
//  State machine tick — called every 1 ms from esp_timer
// ============================================================================
static void keyer_tick_iambic(void) {
  bool dit_now = readDit();
  bool dah_now = readDah();

  // Iambic B: latch only the OPPOSITE paddle during element sounding
  // During KS_DIT, latch dah if pressed. During KS_DAH, latch dit if pressed.
  // NEVER latch the same paddle — that causes double elements from a single tap.
  if (mode == KEYER_IAMBIC_B) {
    if (state == KS_DIT && dah_now) dah_latch = true;
    if (state == KS_DAH && dit_now) dit_latch = true;
  }

  switch (state) {
    // ---- IDLE ----
    case KS_IDLE:
      if (dit_now && dah_now) {
        keyDown(false);
        patternAdd('.');
        timer_ms = dit_ms;
        state = KS_DIT;
        dit_latch = false;
        dah_latch = true;
        idle_ms = 0; space_sent = false;
      } else if (dit_now) {
        keyDown(false);
        patternAdd('.');
        timer_ms = dit_ms;
        state = KS_DIT;
        dit_latch = false;
        dah_latch = false;
        idle_ms = 0; space_sent = false;
      } else if (dah_now) {
        keyDown(true);
        patternAdd('-');
        timer_ms = dit_ms * 3;
        state = KS_DAH;
        dit_latch = false;
        dah_latch = false;
        idle_ms = 0; space_sent = false;
      } else {
        // Word space detection: 4 more dit-lengths after char boundary (total 7)
        if (!space_sent) {
          idle_ms++;
          if (idle_ms >= dit_ms * 4) {
            if (char_cb) char_cb(' ');
            space_sent = true;
          }
        }
      }
      break;

    // ---- DIT sounding ----
    case KS_DIT:
      if (--timer_ms == 0) {
        keyUp();
        timer_ms = dit_ms;
        state = KS_IEG;
      }
      break;

    // ---- DAH sounding ----
    case KS_DAH:
      if (--timer_ms == 0) {
        keyUp();
        timer_ms = dit_ms;
        state = KS_IEG;
      }
      break;

    // ---- Inter-element gap ----
    case KS_IEG:
      if (--timer_ms == 0) {
        bool next_dit, next_dah;

        if (mode == KEYER_IAMBIC_B) {
          // Mode B: latches (opposite only) OR current paddle state
          next_dit = dit_latch || readDit();
          next_dah = dah_latch || readDah();
        } else {
          // Mode A: only current paddle state
          next_dit = readDit();
          next_dah = readDah();
        }

        dit_latch = false;
        dah_latch = false;

        if (next_dit && next_dah) {
          // Alternate: if last was dit → dah, if last was dah → dit
          char last = (pat_len > 0) ? pattern[pat_len - 1] : '.';
          if (last == '.') {
            keyDown(true);
            patternAdd('-');
            timer_ms = dit_ms * 3;
            state = KS_DAH;
          } else {
            keyDown(false);
            patternAdd('.');
            timer_ms = dit_ms;
            state = KS_DIT;
          }
        } else if (next_dit) {
          keyDown(false);
          patternAdd('.');
          timer_ms = dit_ms;
          state = KS_DIT;
        } else if (next_dah) {
          keyDown(true);
          patternAdd('-');
          timer_ms = dit_ms * 3;
          state = KS_DAH;
        } else {
          // No paddle — start character gap wait (2 more dit times)
          timer_ms = dit_ms * 2;
          state = KS_CHAR_WAIT;
        }
      }
      break;

    // ---- Waiting for character boundary ----
    case KS_CHAR_WAIT:
      // If paddle pressed before timeout → continue same character
      if (readDit()) {
        keyDown(false);
        patternAdd('.');
        timer_ms = dit_ms;
        state = KS_DIT;
        dit_latch = false;
        dah_latch = false;
      } else if (readDah()) {
        keyDown(true);
        patternAdd('-');
        timer_ms = dit_ms * 3;
        state = KS_DAH;
        dit_latch = false;
        dah_latch = false;
      } else if (--timer_ms == 0) {
        // Character boundary reached
        emitChar();
        state = KS_IDLE;
      }
      break;
  }
}

// Straight key timing for external access
static uint32_t sk_down_time_copy = 0;

// Straight key event log — written from 1ms ISR, read from UI
// This ensures NO events are missed regardless of UI refresh rate
#define SK_LOG_SIZE 16
static volatile uint16_t sk_log[SK_LOG_SIZE];  // completed element durations in ms
static volatile uint8_t  sk_logW = 0;          // write index (ISR only)
static volatile bool     sk_active = false;     // true while key physically held
static volatile uint16_t sk_held_ms = 0;        // current hold duration, updates every 1ms

// Adaptive straight key timing — learns from operator's actual speed
static uint32_t sk_dit_avg = 80;
#define SK_THRESHOLD()    (sk_dit_avg * 2)
#define SK_CHAR_GAP()     (sk_dit_avg * 3)
#define SK_WORD_GAP()     (sk_dit_avg * 7)

static void keyer_tick_straight(void) {
  bool key_down = readDit() || readDah();

  static bool    was_down   = false;
  static uint32_t up_time   = 0;
  static uint32_t down_time = 0;

  if (key_down) {
    if (!was_down) {
      keyDown(false);
      if (up_time >= SK_CHAR_GAP() && pat_len > 0) {
        emitChar();
      }
      down_time = 0;
      space_sent = false;
      sk_active = true;
    }
    down_time++;
    sk_down_time_copy = down_time;
    sk_held_ms = down_time;
    up_time = 0;
    was_down = true;
  } else {
    if (was_down) {
      keyUp();
      // Push completed element duration to log buffer
      sk_log[sk_logW % SK_LOG_SIZE] = (uint16_t)down_time;
      sk_logW++;
      sk_active = false;
      sk_held_ms = 0;

      // Classify based on adaptive threshold
      if (down_time <= SK_THRESHOLD()) {
        patternAdd('.');
        sk_dit_avg = (sk_dit_avg * 3 + down_time) / 4;
        if (sk_dit_avg < 20) sk_dit_avg = 20;
      } else {
        patternAdd('-');
        uint32_t implied_dit = down_time / 3;
        sk_dit_avg = (sk_dit_avg * 3 + implied_dit) / 4;
        if (sk_dit_avg < 20) sk_dit_avg = 20;
      }
      up_time = 0;
    }
    up_time++;
    if (up_time == SK_CHAR_GAP() && pat_len > 0) {
      emitChar();
    }
    if (!space_sent && up_time == SK_WORD_GAP()) {
      if (char_cb) char_cb(' ');
      space_sent = true;
    }
    was_down = false;
  }
}

// ============================================================================
//  Timer callback (1 ms)
// ============================================================================
static void IRAM_ATTR keyer_timer_cb(void* arg) {
  if (mode == KEYER_STRAIGHT) {
    keyer_tick_straight();
  } else {
    keyer_tick_iambic();
  }
}

// ============================================================================
//  Public API
// ============================================================================
void Keyer_Init(void) {
  pinMode(DIT_PADDLE_PIN, INPUT_PULLUP);
  pinMode(DAH_PADDLE_PIN, INPUT_PULLUP);

  recalcTiming();
  patternReset();

  const esp_timer_create_args_t args = {
    .callback = keyer_timer_cb,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "keyer_tick",
    .skip_unhandled_events = true,
  };
  esp_timer_create(&args, &keyer_timer);
  esp_timer_start_periodic(keyer_timer, 1000);  // 1 ms

  printf("Keyer: init OK  mode=%s  wpm=%d\n", Keyer_ModeName(mode), wpm);
}

void Keyer_SetMode(keyer_mode_t m) {
  if (m < KEYER_MODE_COUNT) {
    mode = m;
    state = KS_IDLE;
    patternReset();
    keyUp();
  }
}
keyer_mode_t Keyer_GetMode(void) { return mode; }

const char* Keyer_ModeName(keyer_mode_t m) {
  static const char* names[] = {"Iambic A", "Iambic B", "Straight"};
  return (m < KEYER_MODE_COUNT) ? names[m] : "?";
}

void Keyer_SetWPM(uint8_t w) {
  if (w < 5)  w = 5;
  if (w > 40) w = 40;
  wpm = w;
  recalcTiming();
}
uint8_t Keyer_GetWPM(void) { return wpm; }

void Keyer_SetSwap(bool s) { swapped = s; }
bool Keyer_GetSwap(void)   { return swapped; }

void Keyer_OnChar(keyer_char_cb_t cb)       { char_cb = cb; }
void Keyer_OnElement(keyer_element_cb_t cb)  { element_cb = cb; }

bool Keyer_IsSending(void) { return sending; }

// Expose straight key timing for visual feedback
uint32_t Keyer_GetDownTime(void) { return sending ? sk_down_time_copy : 0; }
uint32_t Keyer_GetDitMs(void) { return dit_ms; }
uint32_t Keyer_GetSKDitAvg(void) { return sk_dit_avg; }

// Straight key event log API — UI reads these
bool Keyer_SKIsDown(void) { return sk_active; }
uint16_t Keyer_SKHeldMs(void) { return sk_held_ms; }

static uint8_t sk_logR_global = 0;  // read cursor for home screen
static uint8_t sk_logR_game = 0;    // read cursor for game

bool Keyer_SKPopElement(uint16_t* duration_ms, bool forGame) {
  volatile uint8_t* rp = forGame ? (volatile uint8_t*)&sk_logR_game : (volatile uint8_t*)&sk_logR_global;
  if (*rp == sk_logW) return false;
  *duration_ms = sk_log[*rp % SK_LOG_SIZE];
  (*rp)++;
  return true;
}

void Keyer_SKFlush(bool forGame) {
  if (forGame) sk_logR_game = sk_logW;
  else sk_logR_global = sk_logW;
}  // adaptive straight key dit length

const char* Keyer_GetPattern(void) {
  // If currently building a character, show live pattern
  if (pat_len > 0) return pattern;
  // Otherwise show last emitted pattern for 800ms
  if (displayPattern[0] && (millis() - displayPatternTime < 800)) return displayPattern;
  // Expired
  displayPattern[0] = '\0';
  return "";
}

void Keyer_Tick(void) {}
