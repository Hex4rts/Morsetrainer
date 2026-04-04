#ifndef MT_KOCH_H
#define MT_KOCH_H

#include <Arduino.h>

// Standard Koch method order (G4FON / LCWO)
#define KOCH_TOTAL_CHARS  40

// Initialise — loads lesson from SD if available
void Koch_Init(void);

// Current lesson (0-based). Lesson N unlocks characters 0..N
void    Koch_SetLesson(uint8_t lesson);
uint8_t Koch_GetLesson(void);

// Advance to next lesson (clamps at max)
void Koch_Advance(void);

// Reset back to lesson 0
void Koch_Reset(void);

// Number of characters unlocked in the current lesson
uint8_t Koch_UnlockedCount(void);

// Get the full Koch order string (null-terminated, KOCH_TOTAL_CHARS chars)
const char* Koch_Order(void);

// Get a random character from the unlocked set
char Koch_RandomChar(void);

// Check if a character is unlocked in the current lesson
bool Koch_IsUnlocked(char c);

// Persistence (called by Settings module)
void Koch_Save(void);
void Koch_Load(void);

#endif // MT_KOCH_H
