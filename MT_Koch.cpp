#include "MT_Koch.h"
#include <SD_MMC.h>

// ============================================================================
//  Koch order — standard G4FON / LCWO sequence
// ============================================================================
static const char kochOrder[KOCH_TOTAL_CHARS + 1] =
    "KMRSUA"
    "PTLOWI"
    ".NJEF0"
    "Y,VG5/"
    "Q9ZH38"
    "B?427C"
    "1D6X";

static uint8_t currentLesson = 1;  // lesson 1 = K,M  (0-based index → chars 0..lesson)

#define KOCH_FILE "/koch.txt"

// ============================================================================
//  Public API
// ============================================================================
void Koch_Init(void) {
  Koch_Load();
  printf("Koch: init OK  lesson=%d  chars=%d\n", currentLesson, Koch_UnlockedCount());
}

void Koch_SetLesson(uint8_t lesson) {
  if (lesson >= KOCH_TOTAL_CHARS) lesson = KOCH_TOTAL_CHARS - 1;
  currentLesson = lesson;
}

uint8_t Koch_GetLesson(void) { return currentLesson; }

void Koch_Advance(void) {
  if (currentLesson < KOCH_TOTAL_CHARS - 1) {
    currentLesson++;
    Koch_Save();
    printf("Koch: advanced to lesson %d  new char='%c'\n", currentLesson, kochOrder[currentLesson]);
  }
}

void Koch_Reset(void) {
  currentLesson = 1;
  Koch_Save();
}

uint8_t Koch_UnlockedCount(void) {
  return currentLesson + 1;
}

const char* Koch_Order(void) {
  return kochOrder;
}

char Koch_RandomChar(void) {
  uint8_t count = Koch_UnlockedCount();
  return kochOrder[random(0, count)];
}

bool Koch_IsUnlocked(char c) {
  c = toupper(c);
  for (uint8_t i = 0; i <= currentLesson && i < KOCH_TOTAL_CHARS; i++) {
    if (kochOrder[i] == c) return true;
  }
  return false;
}

// ============================================================================
//  Persistence
// ============================================================================
void Koch_Save(void) {
  File f = SD_MMC.open(KOCH_FILE, FILE_WRITE);
  if (f) {
    f.printf("%d\n", currentLesson);
    f.close();
    printf("Koch: saved lesson %d to SD\n", currentLesson);
  }
}

void Koch_Load(void) {
  if (SD_MMC.exists(KOCH_FILE)) {
    File f = SD_MMC.open(KOCH_FILE, FILE_READ);
    if (f) {
      String line = f.readStringUntil('\n');
      int val = line.toInt();
      if (val >= 0 && val < KOCH_TOTAL_CHARS) {
        currentLesson = val;
      }
      f.close();
      printf("Koch: loaded lesson %d from SD\n", currentLesson);
    }
  }
}
