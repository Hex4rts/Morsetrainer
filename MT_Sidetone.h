#ifndef MT_SIDETONE_H
#define MT_SIDETONE_H

#include <Arduino.h>

// Defaults
#define SIDETONE_DEFAULT_FREQ     700     // Hz
#define SIDETONE_DEFAULT_VOL      80      // 0-100
#define SIDETONE_SAMPLE_RATE      44100

// Initialise I2S for tone generation (call once in setup)
void Sidetone_Init(void);

// Start / stop the tone
void Sidetone_On(void);
void Sidetone_Off(void);
bool Sidetone_IsOn(void);

// Set frequency (200-1200 Hz typical)
void Sidetone_SetFreq(uint16_t hz);
uint16_t Sidetone_GetFreq(void);

// Set volume 0-100
void Sidetone_SetVolume(uint8_t vol);
uint8_t Sidetone_GetVolume(void);

#endif // MT_SIDETONE_H
