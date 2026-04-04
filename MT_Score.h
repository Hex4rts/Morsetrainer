#ifndef MT_SCORE_H
#define MT_SCORE_H

#include <Arduino.h>

#define SCORE_MAX_ENTRIES 5   // top-5 per game

typedef struct {
  uint32_t score;
  uint8_t  level;
  char     date[11];  // "YYYY-MM-DD"
} score_entry_t;

typedef struct {
  char          gameName[20];
  score_entry_t entries[SCORE_MAX_ENTRIES];
  uint8_t       count;
} score_board_t;

// Load scores for a game from SD (returns empty board if not found)
score_board_t Score_Load(const char* gameName);

// Submit a new score — inserts if it's a top-5 score, saves to SD
// Returns the rank (0 = 1st place) or -1 if not a high score
int Score_Submit(const char* gameName, uint32_t score, uint8_t level);

// Clear scores for a game
void Score_Clear(const char* gameName);

// Clear all game scores
void Score_ClearAll(void);

#endif // MT_SCORE_H
