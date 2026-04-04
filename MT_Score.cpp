#include "MT_Score.h"
#include <SD_MMC.h>

#define SCORE_DIR "/scores"

static void ensureDir(void) {
  if (!SD_MMC.exists(SCORE_DIR)) {
    SD_MMC.mkdir(SCORE_DIR);
  }
}

static void scorePath(char* buf, size_t len, const char* gameName) {
  snprintf(buf, len, "%s/%s.txt", SCORE_DIR, gameName);
}

// ============================================================================
//  Load
// ============================================================================
score_board_t Score_Load(const char* gameName) {
  score_board_t board;
  memset(&board, 0, sizeof(board));
  strncpy(board.gameName, gameName, sizeof(board.gameName) - 1);

  char path[64];
  scorePath(path, sizeof(path), gameName);

  if (!SD_MMC.exists(path)) return board;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return board;

  while (f.available() && board.count < SCORE_MAX_ENTRIES) {
    String line = f.readStringUntil('\n');
    line.trim();
    // Format: score,level,date
    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) continue;

    score_entry_t* e = &board.entries[board.count];
    e->score = line.substring(0, c1).toInt();
    e->level = line.substring(c1 + 1, c2).toInt();
    strncpy(e->date, line.substring(c2 + 1).c_str(), sizeof(e->date) - 1);
    board.count++;
  }
  f.close();
  return board;
}

// ============================================================================
//  Save
// ============================================================================
static void saveBoard(const score_board_t* board) {
  ensureDir();
  char path[64];
  scorePath(path, sizeof(path), board->gameName);

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return;
  for (int i = 0; i < board->count; i++) {
    const score_entry_t* e = &board->entries[i];
    f.printf("%lu,%d,%s\n", e->score, e->level, e->date);
  }
  f.close();
}

// ============================================================================
//  Submit
// ============================================================================
int Score_Submit(const char* gameName, uint32_t score, uint8_t level) {
  score_board_t board = Score_Load(gameName);

  // Find insertion point (sorted descending)
  int pos = board.count;
  for (int i = 0; i < board.count; i++) {
    if (score > board.entries[i].score) {
      pos = i;
      break;
    }
  }

  if (pos >= SCORE_MAX_ENTRIES) return -1;  // not a high score

  // Shift entries down
  for (int i = SCORE_MAX_ENTRIES - 1; i > pos; i--) {
    board.entries[i] = board.entries[i - 1];
  }

  // Insert new entry
  board.entries[pos].score = score;
  board.entries[pos].level = level;
  // Simple date placeholder — RTC could provide real date
  strncpy(board.entries[pos].date, "----------", sizeof(board.entries[pos].date));
  
  if (board.count < SCORE_MAX_ENTRIES) board.count++;
  strncpy(board.gameName, gameName, sizeof(board.gameName) - 1);

  saveBoard(&board);
  return pos;
}

// ============================================================================
//  Clear
// ============================================================================
void Score_Clear(const char* gameName) {
  char path[64];
  scorePath(path, sizeof(path), gameName);
  if (SD_MMC.exists(path)) SD_MMC.remove(path);
}

void Score_ClearAll(void) {
  Score_Clear("falling");
  Score_Clear("callrush");
}
