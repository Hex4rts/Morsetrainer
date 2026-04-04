#ifndef MT_MORSE_H
#define MT_MORSE_H

#include <Arduino.h>

// Maximum elements in a single Morse character (e.g. '0' = "-----" = 5)
#define MORSE_MAX_LEN  7

// Encode a character to its Morse string (e.g. 'A' → ".-")
// Returns nullptr if character has no Morse representation
const char* Morse_Encode(char c);

// Decode a dit/dah string to a character (e.g. ".-" → 'A')
// Returns '\0' if pattern is invalid
char Morse_Decode(const char* pattern);

// Get the number of elements (dits + dahs) for a character
uint8_t Morse_ElementCount(char c);

#endif // MT_MORSE_H
