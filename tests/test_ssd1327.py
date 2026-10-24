"""验证真实 SSD1327 传输代码的行寻址、错误传播及灰度像素打包。"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
typedef void *esp_lcd_panel_io_handle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_FAIL -2
#define GPIO_MODE_OUTPUT 1
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) ((gpio)>=0 && (gpio)<31)
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_RETURN_ON_FALSE(check,error,tag,...) do {if(!(check))return error;} while(0)
#define ESP_RETURN_ON_ERROR(call,tag,...) do {int r=(call);if(r!=ESP_OK)return r;} while(0)
typedef struct {uint64_t pin_bit_mask;int mode;} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_set_level(int,int);
void vTaskDelay(unsigned);
esp_err_t esp_lcd_panel_io_tx_param(void *,int,const void *,size_t);
esp_err_t esp_lcd_panel_io_tx_color(void *,int,const void *,size_t);
'''
HARNESS = r'''
#include "ssd1327.h"
#include "ssd1327_pixels.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void *io=(void *)1;
static unsigned gpio_calls, delay_ms, params, colors;
static int fail_at, reset_fail, last_command, color_fail;
static bool initializing=true;
static uint8_t columns[2], rows[2];
static const uint8_t *sent;
static size_t sent_size;
esp_err_t gpio_config(const gpio_config_t *c) {
    assert(c->pin_bit_mask==(1ULL<<20) && c->mode==GPIO_MODE_OUTPUT);
    return reset_fail ? ESP_FAIL : ESP_OK;
}
esp_err_t gpio_set_level(int gpio,int level) {
    assert(gpio==20 && level==(gpio_calls==1 ? 0 : 1));++gpio_calls;return ESP_OK;
}
void vTaskDelay(unsigned ms) {delay_ms+=ms;}
esp_err_t esp_lcd_panel_io_tx_param(void *p,int cmd,const void *data,size_t n) {
    assert(p==io);++params;
    if(initializing){assert(cmd!=0xaf);if(params==1)assert(cmd==0xae);}
    if(fail_at && params==(unsigned)fail_at)return ESP_FAIL;
    assert((data && n) || (!data && !n));
    if(cmd==0x15){assert(n==2);memcpy(columns,data,2);}
    if(cmd==0x75){assert(n==2);memcpy(rows,data,2);}
    last_command=cmd;return ESP_OK;
}
esp_err_t esp_lcd_panel_io_tx_color(void *p,int cmd,const void *data,size_t n) {
    assert(p==io && cmd==-1);++colors;sent=data;sent_size=n;return color_fail ? ESP_FAIL : ESP_OK;
}
int main(void) {
    assert(ssd1327_init(NULL,20)==ESP_ERR_INVALID_ARG);
    assert(ssd1327_init(io,-1)==ESP_ERR_INVALID_ARG && !gpio_calls);
    reset_fail=1;
    assert(ssd1327_init(io,20)==ESP_FAIL && !gpio_calls && !params);
    reset_fail=0;
    assert(ssd1327_init(io,20)==ESP_OK && gpio_calls==3 && delay_ms==500);
    /* 初始化期间不能开启面板或提交未初始化显存；开屏由调用方清屏后显式执行。 */
    assert(last_command==0xb9 && colors==0);
    uint8_t guarded[SSD1327_BUFFER_SIZE+2];memset(guarded,0xa5,sizeof guarded);
    uint8_t *pixels=guarded+1;memset(pixels,0xff,SSD1327_BUFFER_SIZE);
    ssd1327_set_pixel(pixels,128,0,0,0);assert(pixels[0]==0x0f);
    ssd1327_set_pixel(pixels,128,1,0,7);assert(pixels[0]==0x07);
    ssd1327_set_pixel(pixels,128,126,127,3);
    ssd1327_set_pixel(pixels,128,127,127,12);assert(pixels[8191]==0x3c);
    /* 灰度条覆盖所有 16 档，跨行地址与相邻半字节互不污染。 */
    for(int x=0;x<16;++x)ssd1327_set_pixel(pixels,128,x,1,(uint8_t)x);
    for(int i=0;i<8;++i)assert(pixels[64+i]==((2*i)<<4 | (2*i+1)));
    assert(pixels[63]==0xff && pixels[72]==0xff && guarded[0]==0xa5 && guarded[8193]==0xa5);
    assert(ssd1327_draw_rows(io,0,128,pixels,8192)==ESP_OK);
    assert(columns[0]==0 && columns[1]==63 && rows[0]==0 && rows[1]==127);
    assert(sent==pixels && sent_size==8192 && colors==1);
    assert(ssd1327_draw_rows(io,127,1,pixels+8128,64)==ESP_OK);
    assert(rows[0]==127 && rows[1]==127 && sent==pixels+8128 && sent_size==64);
    unsigned before=params;
    assert(ssd1327_draw_rows(io,-1,1,pixels,64)==ESP_ERR_INVALID_ARG);
    assert(ssd1327_draw_rows(io,128,1,pixels,64)==ESP_ERR_INVALID_ARG);
    assert(ssd1327_draw_rows(io,127,2,pixels,128)==ESP_ERR_INVALID_ARG);
    assert(ssd1327_draw_rows(io,0,0,pixels,0)==ESP_ERR_INVALID_ARG);
    assert(ssd1327_draw_rows(io,0,16,pixels,1023)==ESP_ERR_INVALID_ARG);
    assert(ssd1327_draw_rows(io,0,16,NULL,1024)==ESP_ERR_INVALID_ARG && params==before);
    /* 任一地址命令失败均停止像素传输，保留最初错误码。 */
    fail_at=params+1;assert(ssd1327_draw_rows(io,0,16,pixels,1024)==ESP_FAIL && colors==2);
    fail_at=params+2;assert(ssd1327_draw_rows(io,0,16,pixels,1024)==ESP_FAIL && colors==2);
    fail_at=0;color_fail=1;
    assert(ssd1327_draw_rows(io,0,16,pixels,1024)==ESP_FAIL && colors==3);
    initializing=false;
    assert(ssd1327_set_on(io,true)==ESP_OK && last_command==0xaf);
    assert(ssd1327_set_on(io,false)==ESP_OK && last_command==0xae);
    puts("PASS: SSD1327 grayscale/nibble packing, neighboring pixels, reset failure, row windows, boundary rejection, DMA buffer forwarding and transport error propagation");
}
'''
with tempfile.TemporaryDirectory(prefix="esp32-ssd1327-") as directory:
    work = Path(directory)
    (work / "sdk.h").write_text(SDK)
    for name in ("esp_err.h", "esp_lcd_panel_io.h", "esp_check.h", "driver/gpio.h",
                 "freertos/FreeRTOS.h", "freertos/task.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "sdk.h"\n')
    (work / "test.c").write_text(HARNESS)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                    "-fsanitize=address,undefined", "-I", str(work),
                    "-I", str(ROOT / "components/display"), str(work / "test.c"),
                    str(ROOT / "components/display/ssd1327.c"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
