#pragma once
#include <stdint.h>

/* SSD1327 水平寻址：偶数 x 在高半字节，灰度 0 为黑、15 为白。 */
static inline void ssd1327_set_pixel(uint8_t *pixels, int width, int x, int y, uint8_t gray)
{
    const int index = y * (width / 2) + x / 2;
    const int shift = (x & 1) ? 0 : 4;
    pixels[index] = (uint8_t)((pixels[index] & ~(0x0fU << shift)) | ((gray & 0x0fU) << shift));
}
