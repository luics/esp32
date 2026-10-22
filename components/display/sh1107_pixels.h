#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SH1107_WIDTH 128
#define SH1107_HEIGHT 128
#define SH1107_BUFFER_SIZE (SH1107_WIDTH * SH1107_HEIGHT / 8)

/* Wokwi SH1107 模型的可见列原点为 96；按页打包，1 表示白色发光像素。 */
static inline void sh1107_set_pixel(uint8_t *pixels, int x, int y, bool white)
{
    const int column = (x + 96) % SH1107_WIDTH;
    const int index = (y / 8) * SH1107_WIDTH + column;
    const uint8_t mask = 1U << (y % 8);
    if (white) pixels[index] |= mask;
    else pixels[index] &= (uint8_t)~mask;
}
