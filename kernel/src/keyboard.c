#include "keyboard.h"

// Scan Code Set 1 → ASCII, for a standard US keyboard layout.
// Index = scan code, value = ASCII character (0 = unmapped/not needed yet).
static const char scancode_table[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
    // Everything past this point (F-keys, arrows, numpad, etc.) is 0/unmapped for now
};

char scancode_to_ascii(uint8_t scancode) {
    if (scancode >= 128) return 0;
    return scancode_table[scancode];
}

// A tiny circular buffer to hold typed characters until the shell reads them.
#define KB_BUFFER_SIZE 64
static char kb_buffer[KB_BUFFER_SIZE];
static int kb_head = 0;
static int kb_tail = 0;

void keyboard_handle_scancode(uint8_t scancode) {
    if (scancode & 0x80) {
        return; // key release, ignore
    }

    char c = scancode_to_ascii(scancode);
    if (c == 0) {
        return; // unmapped key, ignore
    }

    int next_head = (kb_head + 1) % KB_BUFFER_SIZE;
    if (next_head != kb_tail) { // only add if buffer isn't full
        kb_buffer[kb_head] = c;
        kb_head = next_head;
    }
}

char keyboard_read_char(void) {
    if (kb_head == kb_tail) {
        return 0; // buffer empty
    }
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}
