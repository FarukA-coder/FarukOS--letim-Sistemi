#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

extern volatile int32_t mouse_x;
extern volatile int32_t mouse_y;
extern volatile uint8_t mouse_left_click;
extern volatile uint8_t mouse_right_click;

void init_mouse();
void mouse_handler();

#endif
