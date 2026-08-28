#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// 8x8 bitmap font, one row per byte, MSB = leftmost pixel.
// Covers '0'-'9' and 'A'-'F' — enough for hex output.
extern const uint8_t font_8x8[][8];

// Given a character, returns its index into font_8x8, or -1 if unsupported.
int font_char_index(char c);

#endif
