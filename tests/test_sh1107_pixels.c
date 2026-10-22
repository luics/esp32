#include "sh1107_pixels.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t pixels[SH1107_BUFFER_SIZE];
    memset(pixels, 0, sizeof(pixels));
    /* 按 Wokwi 模型反向读取全部像素，检查跨页、列回绕及黑白极性。 */
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            sh1107_set_pixel(pixels, x, y, (x + y) % 3 != 0);
        }
    }
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const bool white = pixels[y / 8 * 128 + (x + 96) % 128] & (1U << (y % 8));
            assert(white == ((x + y) % 3 != 0));
        }
    }
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) sh1107_set_pixel(pixels, x, y, true);
    }
    for (size_t i = 0; i < sizeof(pixels); ++i) assert(pixels[i] == 0xff);
    sh1107_set_pixel(pixels, 119, 7, false);
    sh1107_set_pixel(pixels, 119, 8, false);
    assert(pixels[(119 + 96) % 128] == 0x7f);
    assert(pixels[128 + (119 + 96) % 128] == 0xfe);
    puts("PASS: SH1107 monochrome packing, page boundaries, column origin and white background");
}
