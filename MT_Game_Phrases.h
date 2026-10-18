#ifndef MT_GAME_PHRASES_H
#define MT_GAME_PHRASES_H

#include <Arduino.h>

void Game_Phrases_Start(void);
void Game_Phrases_Stop(void);
void Game_Phrases_Update(void);

// Phrase database (code + meaning), shared with the ABC tab reference page
uint8_t     Phrases_Count(void);
const char* Phrases_Code(uint8_t i);
const char* Phrases_Meaning(uint8_t i);

#endif
