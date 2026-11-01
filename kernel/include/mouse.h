#ifndef LIXOS_MOUSE_H
#define LIXOS_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

struct MouseState {
    int32_t x;
    int32_t y;
    bool    left_btn;
    bool    right_btn;
    bool    middle_btn;
};

void mouse_init(void);
MouseState mouse_get_state(void);
void mouse_set_position(int32_t x, int32_t y);
void mouse_draw_cursor(int32_t x, int32_t y);

#endif // LIXOS_MOUSE_H
