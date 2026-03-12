#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>
void init_mouse();
void handle_mouse();
int get_mouse_x();
int get_mouse_y();
int mouse_left_pressed();
#endif
