"""在主机上运行真实 app_main，替换外设，验证红键声光边沿和绿键隔离。"""
from pathlib import Path
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_INVALID_ARG=-1, ESP_ERR_NO_MEM=-2 };
#define ESP_ERROR_CHECK(value) assert((value) == ESP_OK)
static inline void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define CONFIG_BLINK_RGB_GPIO 8
#define CONFIG_BEEP_GPIO 3
#define CONFIG_BEEP_FREQUENCY_HZ 2000
#define CONFIG_INTERVAL_CONTROL_ADC_GPIO 4
#define CONFIG_INTERVAL_CONTROL_SAMPLE_INTERVAL_MS 50
#define CONFIG_BUTTON_INPUT_RED_GPIO 10
#define CONFIG_BUTTON_INPUT_GPIO 9
#define CONFIG_BUTTON_INPUT_BLUE_GPIO 11
#define CONFIG_DISPLAY_SCLK_GPIO 6
#define CONFIG_DISPLAY_MOSI_GPIO 7
#define CONFIG_DISPLAY_CS_GPIO 18
#define CONFIG_DISPLAY_DC_GPIO 19
#define CONFIG_DISPLAY_RST_GPIO 20
#define CONFIG_DISPLAY_WOKWI_SH1107 1
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, size_t size);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned wait);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned wait);
int xQueueReset(QueueHandle_t queue);
void vTaskDelay(TickType_t ticks);
void vTaskPrioritySet(TaskHandle_t task, unsigned priority);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(int clear, unsigned wait);
int gpio_get_level(int gpio);
int64_t esp_timer_get_time(void);
const char *esp_err_to_name(esp_err_t code);
void esp_restart(void);
'''
HARNESS = r'''
#include "test_sdk.h"
#include "blink.h"
#include "beep.h"
#include "display.h"
#include "button_input.h"
#include "interval_control.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>

void app_main(void);
struct test_queue { size_t size; unsigned head, count; unsigned char items[64][64]; };
static QueueHandle_t queue;
static uint64_t clock_ms;
static bool sound, light;
static unsigned starts, stops;
static unsigned task_priority = 1;
static uint64_t started_at, durations[14];
static display_frame_t last_frame;
static button_input_edge_callback_t red_edge;
static button_input_callback_t green_press;
static void *red_context, *green_context;
static jmp_buf done;

QueueHandle_t xQueueCreate(unsigned count, size_t size) {
    assert(count==64 && size<=64); queue=calloc(1,sizeof(*queue)); assert(queue); queue->size=size; return queue;
}
int xQueueSend(QueueHandle_t q,const void *item,unsigned wait) {
    (void)wait; if(q->count==64)return 0;
    memcpy(q->items[(q->head+q->count)%64],item,q->size); ++q->count; return pdTRUE;
}
int xQueueReceive(QueueHandle_t q,void *item,unsigned wait) {
    if(!q->count && wait)vTaskDelay(wait);
    if(!q->count)return 0;
    memcpy(item,q->items[q->head],q->size);q->head=(q->head+1)%64;--q->count;return pdTRUE;
}
int xQueueReset(QueueHandle_t q){q->head=q->count=0;return pdTRUE;}
TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void*)1;}
void xTaskNotifyGive(TaskHandle_t task){(void)task;}
void vTaskPrioritySet(TaskHandle_t task,unsigned priority){(void)task;task_priority=priority;}
unsigned ulTaskNotifyTake(int clear,unsigned wait){(void)clear;(void)wait;return 0;}
int gpio_get_level(int gpio){(void)gpio;return 1;}
int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
const char *esp_err_to_name(esp_err_t code){(void)code;return "test";}
void esp_restart(void){assert(!"Unexpected restart");}
esp_err_t blink_create(int gpio,blink_handle_t *handle){assert(gpio==8);*handle=(void*)1;return ESP_OK;}
esp_err_t blink_set_color(blink_handle_t handle,blink_color_t color){(void)handle;(void)color;light=true;return ESP_OK;}
esp_err_t blink_off(blink_handle_t handle){(void)handle;light=false;return ESP_OK;}
esp_err_t beep_init(int gpio,uint32_t hz){assert(gpio==3 && hz==2000);return ESP_OK;}
esp_err_t beep_set_active(bool active){
    if(active){assert(!sound && starts<14);started_at=clock_ms;++starts;}
    else if(sound){durations[stops++]=clock_ms-started_at;}
    sound=active;return ESP_OK;
}
esp_err_t interval_control_start(int gpio,uint32_t interval){assert(gpio==4 && interval==50);return ESP_OK;}
uint32_t interval_control_get_unit_ms(void){return clock_ms>=400 ? 60 : 100;}
esp_err_t display_init(const display_config_t *config,display_handle_t *handle){(void)config;*handle=(void*)1;return ESP_OK;}
esp_err_t display_show_frame(display_handle_t handle,const display_frame_t *frame){(void)handle;last_frame=*frame;return ESP_OK;}
esp_err_t button_input_create_edges(int gpio,button_input_edge_callback_t callback,void *ctx,button_input_handle_t *handle){
    assert(gpio==10);red_edge=callback;red_context=ctx;*handle=(void*)1;return ESP_OK;
}
esp_err_t button_input_create(int gpio,button_input_callback_t callback,void *ctx,button_input_handle_t *handle){
    if(gpio==9){green_press=callback;green_context=ctx;}else assert(gpio==11);
    *handle=(void*)1;return ESP_OK;
}
void vTaskDelay(TickType_t ticks){
    /* 检查真实主循环在本轮处理边沿后的输出；不模拟或复述输入处理分支。 */
    if(clock_ms==20 || clock_ms==100 || clock_ms==350)assert(sound && light);
    if(clock_ms==50 || clock_ms==300 || (clock_ms>=850 && clock_ms<1040) || (clock_ms>=1440 && clock_ms<2200) || clock_ms>=2700)assert(!sound && !light);
    if(clock_ms==70){assert(starts==1 && stops==1);assert(!strcmp(last_frame.current_code,"."));}
    if(clock_ms==400 || clock_ms==450){
        assert(sound && light && starts==3 && stops==2);
        assert(!strcmp(last_frame.current_code,"..") && last_frame.character_unit_ms==100);
    }
    if(clock_ms==900){assert(starts==3 && stops==3);assert(!strcmp(last_frame.current_code,"..-"));}
    /* 初始常亮；红键按住与七单位静默内常灭，绿键和换档不重置指示。
     * 1420ms 松开的字符仍锁定 100ms，下一字符才使用旋钮的 60ms。 */
    if(clock_ms<20 || (clock_ms>=2120 && clock_ms<2200) || clock_ms>=3120)
        assert(last_frame.word_ready);
    else assert(!last_frame.word_ready);
    if(clock_ms==1500)assert(!strcmp(last_frame.current_code,"INVALID"));
    if(clock_ms>=3200){
        assert(starts==14 && stops==14 && durations[0]==30 && durations[1]==200 && durations[2]==500);
        for(unsigned i=3;i<13;++i)assert(durations[i]==20);
        assert(durations[13]==500);
        longjmp(done,1);
    }
    /* 显示任务优先级为 2：持续就绪时，低优先级输入任务会错过完整短按。
     * 逐个注入驱动边沿，保留原始时刻，覆盖队列中按下/松开挤在一起的情形。 */
    const uint64_t until = clock_ms + ticks +
        (getenv("TEST_RENDER_LOAD") && task_priority <= 2 ? 80 : 0);
    while(clock_ms < until){
    ++clock_ms;
    if(clock_ms>=1040 && clock_ms<=1420){
        if((clock_ms-1040)%40==0)red_edge(true,red_context);
        if((clock_ms-1040)%40==20)red_edge(false,red_context);
    }
    switch(clock_ms){
        case 20:case 100:case 350:case 2200:red_edge(true,red_context);break;
        case 50:case 300:case 850:case 2700:red_edge(false,red_context);break;
        case 70:case 400:case 450:case 900:case 2160:case 2800:green_press(green_context);break;
    }
    }
}
int main(void){
    if(!setjmp(done))app_main();
    free(queue);
    puts("PASS: actual app_main down=on/up=off, 30/200/500ms holds, ten rapid 20ms taps, no replay, 2kHz, green isolated, speed locked, initial/held/7-unit word indicator");
}
'''
with tempfile.TemporaryDirectory(prefix="esp32-feedback-") as directory:
    work = Path(directory)
    (work / "test_sdk.h").write_text(SDK)
    for name in ("driver/gpio.h", "esp_err.h", "esp_log.h", "esp_timer.h", "esp_system.h",
                 "freertos/FreeRTOS.h", "freertos/queue.h", "freertos/task.h", "sdkconfig.h"):
        header = work / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "test_sdk.h"\n')
    (work / "harness.c").write_text(HARNESS)
    includes = [work, ROOT / "main"]
    includes += [ROOT / "components" / name / "include"
                 for name in ("blink", "beep", "display", "button_input", "interval_control")]
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined"]
    for include in includes:
        command += ["-I", str(include)]
    command += [str(work / "harness.c"), str(ROOT / "main/app_main.c"), str(ROOT / "main/morse.c"),
                str(ROOT / "main/telegraph.c"), "-o", str(work / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, env={**os.environ, "TEST_RENDER_LOAD": "1"})
