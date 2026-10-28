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
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define CONFIG_TELEGRAPH_HA_ENABLED 1
#define CONFIG_BLINK_RGB_GPIO 8
#define CONFIG_BLINK_MODE_GPIO 5
#define CONFIG_PIR_SENSOR_GPIO 15
#define CONFIG_BEEP_GPIO 3
#define CONFIG_BEEP_FREQUENCY_HZ 2000
#ifdef TEST_INTERVAL_DISABLED
#define CONFIG_INTERVAL_CONTROL_ADC_GPIO 0 /* 未使用的 ADC 配置不应与红键冲突。 */
#else
#define CONFIG_INTERVAL_CONTROL_ENABLED 1
#define CONFIG_INTERVAL_CONTROL_ADC_GPIO 4
#endif
#define CONFIG_INTERVAL_CONTROL_SAMPLE_INTERVAL_MS 50
#define CONFIG_BUTTON_INPUT_RED_GPIO 0
#define CONFIG_BUTTON_INPUT_GPIO 9
#define CONFIG_BUTTON_INPUT_BLUE_GPIO 11
#define CONFIG_BUTTON_INPUT_BLACK_GPIO 1
#define CONFIG_BUTTON_INPUT_MODE_GPIO 2
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
int xQueueOverwrite(QueueHandle_t queue, const void *item);
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
uint32_t esp_random(void);
'''
HARNESS = r'''
#include "test_sdk.h"
#include "blink.h"
#include "beep.h"
#include "display.h"
#include "button_input.h"
#include "interval_control.h"
#include "pir_sensor.h"
#include "telegraph_sync.h"
#include "telegraph_ha.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>

void app_main(void);
struct test_queue { size_t size; unsigned head, count; unsigned char items[64][64]; };
static QueueHandle_t queue;
static uint64_t clock_ms;
static bool sound, light, startup_checked;
static unsigned starts, stops;
static bool mode_light;
static blink_color_t external_color;
static unsigned external_r, external_g, external_b;
static unsigned external_red, external_green;
static unsigned mode_flashes;
static uint64_t mode_started_at;
static unsigned task_priority = 1;
static uint64_t started_at, durations[14], start_times[14];
static blink_color_t last_color;
static display_frame_t last_frame;
static button_input_edge_callback_t red_edge, black_edge;
static button_input_callback_t green_press, mode_press;
static telegraph_dp_content_callback_t dp_callback;
static void *dp_context;
static void *red_context, *green_context, *black_context, *mode_context;
static jmp_buf done;

QueueHandle_t xQueueCreate(unsigned count, size_t size) {
    assert((count==64 || count==1) && size<=64); QueueHandle_t result=calloc(1,sizeof(*result)); assert(result); result->size=size; if(count==64)queue=result; return result;
}
int xQueueOverwrite(QueueHandle_t q,const void *item){memcpy(q->items[0],item,q->size);q->head=0;q->count=1;return pdTRUE;}
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
uint32_t esp_random(void){static unsigned index; const uint32_t values[]={0,26,35}; assert(index<3); return values[index++];}
static telegraph_ha_state_t ha_state;
esp_err_t telegraph_ha_start(void){return ESP_OK;}
void telegraph_ha_update(const telegraph_ha_state_t *state){ha_state=*state;}
bool telegraph_ha_take_mode(bool *dual){
    if(!getenv("TEST_HA_MODE"))return false;
    static bool first, second;
    if(clock_ms>=50 && !first){first=true;*dual=false;return true;}
    if(clock_ms>=500 && !second){second=true;*dual=true;return true;}
    return false;
}
esp_err_t telegraph_sync_start(const char label[4]){assert(!strcmp(label,"A09"));return ESP_OK;}
esp_err_t telegraph_sync_history(const morse_t *morse){assert(morse->count);return ESP_OK;}
esp_err_t telegraph_dp_post(const char *json){assert(strstr(json,"\"content\":\"hi\""));assert(strstr(json,"\"reasoning_effort\":\"none\""));return ESP_OK;}
void telegraph_dp_set_content_callback(telegraph_dp_content_callback_t callback,void *context){dp_callback=callback;dp_context=context;}
esp_err_t blink_create(int gpio,blink_handle_t *handle){assert(gpio==8);*handle=(void*)1;return ESP_OK;}
esp_err_t blink_create_with_order(int gpio,blink_order_t order,blink_handle_t *handle){
    assert(gpio==5 && order==BLINK_ORDER_GRB);*handle=(void*)2;return ESP_OK;
}
esp_err_t blink_set_color(blink_handle_t handle,blink_color_t color){
    if(handle==(void*)2){
        mode_light=true;external_color=color;
        if(color==BLINK_COLOR_YELLOW){++mode_flashes;mode_started_at=clock_ms;}
        else if(color==BLINK_COLOR_RED)++external_red;
        else {assert(color==BLINK_COLOR_GREEN);++external_green;}
    }
    else {assert(handle==(void*)1 && color!=BLINK_COLOR_YELLOW);last_color=color;light=true;}
    return ESP_OK;
}
esp_err_t blink_set_rgb(blink_handle_t handle,uint8_t red,uint8_t green,uint8_t blue){
    assert(handle==(void*)2);mode_light=true;
    external_r=red;external_g=green;external_b=blue;
    if(red==64 && green==64 && blue==0){
        external_color=BLINK_COLOR_YELLOW;
        if(!getenv("TEST_PIR_MODE") || (clock_ms>=100 && clock_ms<300)){++mode_flashes;mode_started_at=clock_ms;}
    }
    else if(red==64 && green==0 && blue==0){external_color=BLINK_COLOR_RED;++external_red;}
    else if(red==0 && green==64 && blue==0){external_color=BLINK_COLOR_GREEN;++external_green;}
    return ESP_OK;
}
esp_err_t blink_off(blink_handle_t handle){
    if(handle==(void*)2)mode_light=false;
    else {assert(handle==(void*)1);light=false;}
    return ESP_OK;
}
esp_err_t pir_sensor_create(int gpio,pir_sensor_handle_t *handle){assert(gpio==15);*handle=(void*)3;return ESP_OK;}
esp_err_t pir_sensor_sample(pir_sensor_handle_t handle,uint64_t now,pir_sensor_sample_t *sample){
    assert(handle==(void*)3);static bool previous;static uint64_t since;
    bool active=getenv("TEST_PIR_MODE") && now>=50 && now<4650;
    if(active && !previous)since=now;
    *sample=(pir_sensor_sample_t){.motion=active,.changed=active!=previous,.active_ms=active?(uint32_t)(now-since):0};
    previous=active;return ESP_OK;
}
esp_err_t beep_init(int gpio,uint32_t hz){assert(gpio==3 && hz==2000);return ESP_OK;}
esp_err_t beep_set_active(bool active){
    if(active){assert(!sound && starts<14);started_at=clock_ms;start_times[starts++]=clock_ms;}
    else if(sound){durations[stops++]=clock_ms-started_at;}
    sound=active;return ESP_OK;
}
esp_err_t interval_control_start(int gpio,uint32_t interval){
#ifdef TEST_INTERVAL_DISABLED
    (void)gpio;(void)interval;assert(!"Disabled potentiometer must not initialize ADC");
#else
    assert(gpio==4 && interval==50);
#endif
    return ESP_OK;
}
uint32_t interval_control_get_unit_ms(void){
#ifdef TEST_INTERVAL_DISABLED
    return INTERVAL_CONTROL_DEFAULT_UNIT_MS;
#else
    return clock_ms>=400 ? 60 : 100;
#endif
}
esp_err_t display_init(const display_config_t *config,display_handle_t *handle){(void)config;*handle=(void*)1;return ESP_OK;}
esp_err_t display_show_frame(display_handle_t handle,const display_frame_t *frame){(void)handle;assert(!strcmp(frame->label,"A09"));last_frame=*frame;return ESP_OK;}
esp_err_t button_input_create_edges(int gpio,button_input_edge_callback_t callback,void *ctx,button_input_handle_t *handle){
    if(gpio==CONFIG_BUTTON_INPUT_RED_GPIO){red_edge=callback;red_context=ctx;}
    else {assert(gpio==CONFIG_BUTTON_INPUT_BLACK_GPIO);black_edge=callback;black_context=ctx;}
    *handle=(void*)1;return ESP_OK;
}
esp_err_t button_input_create(int gpio,button_input_callback_t callback,void *ctx,button_input_handle_t *handle){
    if(gpio==CONFIG_BUTTON_INPUT_GPIO){green_press=callback;green_context=ctx;}
    else if(gpio==CONFIG_BUTTON_INPUT_MODE_GPIO){mode_press=callback;mode_context=ctx;}else assert(gpio==CONFIG_BUTTON_INPUT_BLUE_GPIO);
    *handle=(void*)1;return ESP_OK;
}
static void dual_step(TickType_t ticks){
    /* 在真实 app_main 中验证快速点击 FIFO、标准声光、长按重复与活动信号内切换。 */
    if(clock_ms<2660)assert(last_frame.dual_mode);
    if(clock_ms>=2660 && clock_ms<2900)assert(!last_frame.dual_mode);
    if(clock_ms>=2900 && clock_ms<2980)assert(last_frame.dual_mode);
    if(clock_ms>=2980)assert(!last_frame.dual_mode);
    if(clock_ms==40)assert(sound && light && last_color==BLINK_COLOR_RED);
    if(clock_ms==230 || clock_ms==2500)assert(sound && light && last_color==BLINK_COLOR_GREEN);
    if(clock_ms==2500)assert(mode_light && external_color==BLINK_COLOR_YELLOW);
    if(clock_ms==2700)assert(!mode_light);
    if(clock_ms==1900)assert(last_frame.word_ready);
    if(clock_ms>=3650){
        const uint64_t expected_starts[]={30,230,630,900,1100,2000,2240,2480,2800,2920,3140};
        const uint64_t expected_durations[]={100,300,100,100,100,180,180,180,30,60,20};
        assert(starts==11 && stops==11 && !sound && !light && !mode_light && mode_flashes==3 && last_frame.word_ready);
        assert(external_red && external_green);
        for(unsigned i=0;i<11;++i){
            assert(start_times[i]==expected_starts[i]);
            assert(durations[i]==expected_durations[i]);
        }
        longjmp(done,1);
    }
    for(uint64_t until=clock_ms+ticks;clock_ms<until;){
        ++clock_ms;
        switch(clock_ms){
            case 2500:case 2900:case 2940:mode_press(mode_context);break;
            case 30:case 70:case 900:case 2520:case 2800:case 2920:case 3140:red_edge(true,red_context);break;
            case 40:case 80:case 1150:case 2530:case 2830:case 3100:case 3160:red_edge(false,red_context);break;
            case 50:case 2000:case 2850:case 3010:black_edge(true,black_context);break;
            case 60:case 2640:case 2860:case 3020:black_edge(false,black_context);break;
            case 400:green_press(green_context);break;
        }
    }
}
void vTaskDelay(TickType_t ticks){
    if(getenv("TEST_DP_PREVIEW")){
        if(clock_ms==20){assert(last_frame.dp_preview_active);assert(!strcmp(last_frame.dp_preview[0],"12345678901234567890"));assert(!strcmp(last_frame.dp_preview[1],"abc?"));}
        if(clock_ms==40)assert(!last_frame.dp_preview_active);
        if(clock_ms>=500){assert(last_frame.dp_preview_active);longjmp(done,1);}
        for(uint64_t until=clock_ms+ticks;clock_ms<until;){
            ++clock_ms;
            if(clock_ms==10){dp_callback(true,NULL,dp_context);dp_callback(false,"12345678901234567890abc\xe4\xbd\xa0",dp_context);}
            if(clock_ms==30)red_edge(true,red_context);
            if(clock_ms==50)red_edge(false,red_context);
        }
        return;
    }
    if(getenv("TEST_HA_MODE")){
        /* 远端请求在划发送中到达：保持完整 300ms，实际切换后才上报 S。 */
        if(clock_ms==100)assert(ha_state.dual_mode && sound);
        if(clock_ms==400)assert(!ha_state.dual_mode && !sound && durations[0]==300);
        if(clock_ms>=550){assert(ha_state.dual_mode && ha_state.unit_ms==100);longjmp(done,1);}
        for(uint64_t until=clock_ms+ticks;clock_ms<until;){
            ++clock_ms;
            if(clock_ms==20)black_edge(true,black_context);
            if(clock_ms==30)black_edge(false,black_context);
        }
        return;
    }
    if(getenv("TEST_PIR_MODE")){
        if(clock_ms==50)assert(mode_light && external_r==64 && external_g==64 && external_b==64);
        if(clock_ms==110)assert(mode_light && external_color==BLINK_COLOR_YELLOW);
        if(clock_ms==550)assert(mode_light && external_r==64 && external_g==64 && external_b==64);
        if(clock_ms==1250)assert(mode_light && external_r==0 && external_g==64 && external_b==0);
        if(clock_ms==1510)assert(light && sound && mode_light && external_g==0 && external_b==0);
        if(clock_ms==2150)assert(mode_light && external_r==64 && external_g==64 && external_b==0);
        if(clock_ms==4150)assert(mode_light && external_r==64 && external_g==0 && external_b==0);
        if(clock_ms>=4660){assert(!mode_light && !light && !sound && mode_flashes==1);longjmp(done,1);}
        for(uint64_t until=clock_ms+ticks;clock_ms<until;){
            ++clock_ms;
            if(clock_ms==100)mode_press(mode_context);
            if(clock_ms==1500)red_edge(true,red_context);
            if(clock_ms==1520)red_edge(false,red_context);
        }
        return;
    }
    /* 外接灯镜像点划；黄光优先且到期恢复当前发送颜色，两个句柄不可串色。 */
    const bool yellow = mode_flashes && clock_ms < mode_started_at + 200;
    assert(mode_light == (yellow || light));
    if(yellow)assert(external_color==BLINK_COLOR_YELLOW);
    else if(light)assert(external_color==last_color);
#ifdef TEST_INTERVAL_DISABLED
    clock_ms+=ticks;
    assert(last_frame.selected_unit_ms==100 && last_frame.character_unit_ms==100);
    assert(!sound && !light);
    if(clock_ms>=1000)longjmp(done,1);
    return;
#endif

    if(!startup_checked){
        assert(clock_ms==0 && last_frame.dual_mode);
        startup_checked=true;
        /* 单键回归必须通过真实模式键从默认双键模式切入，不修改应用状态。 */
        if(!getenv("TEST_DUAL_MODE")){mode_press(mode_context);return;}
    }
    if(getenv("TEST_DUAL_MODE")){dual_step(ticks);return;}
    assert(!last_frame.dual_mode);
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
        assert(mode_flashes==1 && !mode_light && external_red && external_green);
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
#ifdef TEST_INTERVAL_DISABLED
    puts("PASS: actual app_main skips ADC initialization, releases unused ADC pin and stays at 100 ms");return 0;
#endif
    if(getenv("TEST_HA_MODE")){puts("PASS: remote S/D requests preserve complete dash and report actual mode after transition");return 0;}
    if(getenv("TEST_DP_PREVIEW")){puts("PASS: DP preview, wrapping, UTF-8 fallback and input priority");return 0;}
    if(getenv("TEST_DUAL_MODE")){puts("PASS: actual app_main default dual mode, dual FIFO, standard dot/dash, repeats, speed lock, mode switches, inactive black key");return 0;}
    if(getenv("TEST_PIR_MODE")){puts("PASS: PIR white/green/yellow/red blink, mode and dot override, motion ended LED off");return 0;}
    puts("PASS: actual app_main down=on/up=off, 30/200/500ms holds, ten rapid 20ms taps, no replay, 2kHz, green isolated, speed locked, initial/held/7-unit word indicator");
}
'''
with tempfile.TemporaryDirectory(prefix="esp32-feedback-") as directory:
    work = Path(directory)
    (work / "test_sdk.h").write_text(SDK)
    for name in ("driver/gpio.h", "esp_err.h", "esp_log.h", "esp_timer.h", "esp_system.h", "esp_random.h",
                 "freertos/FreeRTOS.h", "freertos/queue.h", "freertos/task.h", "sdkconfig.h"):
        header = work / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "test_sdk.h"\n')
    (work / "harness.c").write_text(HARNESS)
    includes = [work]
    includes += [ROOT / "components" / name / "include"
                 for name in ("blink", "beep", "display", "button_input", "interval_control",
                              "morse", "telegraph", "telegraph_app", "telegraph_sync", "telegraph_ha", "pir_sensor")]
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined"]
    for include in includes:
        command += ["-I", str(include)]
    command += [str(work / "harness.c"), str(ROOT / "main/app_main.c"),
                str(ROOT / "components/telegraph_app/telegraph_app.c"), str(ROOT / "components/morse/morse.c"),
                str(ROOT / "components/telegraph/telegraph.c"), str(ROOT / "components/telegraph/keyer.c"), "-o", str(work / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True, env={**os.environ, "TEST_RENDER_LOAD": "1"})
    subprocess.run([str(work / "test")], check=True, env={**os.environ, "TEST_DUAL_MODE": "1"})
    subprocess.run([str(work / "test")], check=True, env={**os.environ, "TEST_PIR_MODE": "1"})

    subprocess.run([str(work / "test")], check=True, env={**os.environ, "TEST_HA_MODE": "1"})
    subprocess.run([str(work / "test")], check=True, env={**os.environ, "TEST_DP_PREVIEW": "1"})

    subprocess.run(command + ["-DTEST_INTERVAL_DISABLED"], check=True)
    subprocess.run([str(work / "test")], check=True)
