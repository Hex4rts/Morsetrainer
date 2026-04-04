#include "MT_Morse.h"

// ============================================================================
//  Morse code lookup table  (ITU-R M.1677-1)
//  '.' = dit,  '-' = dah
// ============================================================================
struct MorseEntry {
  char ch;
  const char* code;
};

static const MorseEntry morseTable[] = {
  // Letters
  {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},  {'D', "-.."},
  {'E', "."},      {'F', "..-."},   {'G', "--."},   {'H', "...."},
  {'I', ".."},     {'J', ".---"},   {'K', "-.-"},   {'L', ".-.."},
  {'M', "--"},     {'N', "-."},     {'O', "---"},   {'P', ".--."},
  {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},   {'T', "-"},
  {'U', "..-"},    {'V', "...-"},   {'W', ".--"},   {'X', "-..-"},
  {'Y', "-.--"},   {'Z', "--.."},
  // Digits
  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
  {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
  {'8', "---.."}, {'9', "----."},
  // Punctuation
  {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
  {'=', "-...-"},  {'+', ".-.-."},  {'-', "-....-"},
  {'(', "-.--."}, {')', "-.--.-"},
  {'@', ".--.-."}, {'!', "-.-.--"},
  {'\0', nullptr}  // sentinel
};

// -------------------------------------------------------------------
const char* Morse_Encode(char c) {
  c = toupper(c);
  for (const MorseEntry* e = morseTable; e->code; e++) {
    if (e->ch == c) return e->code;
  }
  return nullptr;
}

char Morse_Decode(const char* pattern) {
  if (!pattern || !pattern[0]) return '\0';
  for (const MorseEntry* e = morseTable; e->code; e++) {
    if (strcmp(e->code, pattern) == 0) return e->ch;
  }
  return '\0';
}

uint8_t Morse_ElementCount(char c) {
  const char* code = Morse_Encode(c);
  return code ? strlen(code) : 0;
}
