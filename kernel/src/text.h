#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

void draw_char(char c, uint32_t screen_x, uint32_t screen_y, uint32_t color);
void draw_string(const char *str, uint32_t screen_x, uint32_t screen_y, uint32_t color);

#endif
