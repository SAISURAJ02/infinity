#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Returns the ASCII character for a given scan code, or 0 if unmapped
// (e.g. Shift, Ctrl, arrow keys — not handled yet, keep it simple).
char scancode_to_ascii(uint8_t scancode);

// Called by the keyboard IRQ handler on every key press.
// Pushes the resulting character into a small internal buffer.
void keyboard_handle_scancode(uint8_t scancode);

// Called by the shell to read one character, if available.
// Returns 0 if no character is waiting.
char keyboard_read_char(void);

#endif
