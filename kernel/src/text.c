#include "text.h"
#include "font.h"

extern uint32_t *g_fb_ptr;
extern uint64_t  g_fb_pitch;

void draw_char(char c, uint32_t screen_x, uint32_t screen_y, uint32_t color) {
    int char_index = font_char_index(c);
    if (char_index == -1) {
        return; // unsupported character, draw nothing
    }

    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            if (font_8x8[char_index][y] & (0x80 >> x)) {
                g_fb_ptr[(screen_y + y) * (g_fb_pitch / 4) + (screen_x + x)] = color;
            }
        }
    }
}

void draw_string(const char *str, uint32_t screen_x, uint32_t screen_y, uint32_t color) {
    uint32_t cursor_x = screen_x;
    while (*str != '\0') {
        draw_char(*str, cursor_x, screen_y, color);
        cursor_x += 9; // 8 pixels wide + 1 pixel gap between characters
        str++;
    }
}
