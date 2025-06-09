#include "encoder.h"
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define ENC_A_GPIO 22  // SCL pin on 5-pin JST
#define ENC_B_GPIO 21  // SDA pin on 5-pin JST
#define PCNT_UNIT_USED PCNT_UNIT_0

void encoder_init(void)
{
    // Ensure lines are inputs with internal pull-ups (in case external board pull-ups are weak)
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<ENC_A_GPIO) | (1ULL<<ENC_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);

    // Simple encoder config: count A pulses, use B for direction
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = ENC_A_GPIO,
        .ctrl_gpio_num  = ENC_B_GPIO,
        .channel        = PCNT_CHANNEL_0,
        .unit           = PCNT_UNIT_USED,
        .pos_mode       = PCNT_COUNT_INC,      // rising edge = +1
        .neg_mode       = PCNT_COUNT_DIS,      // don't count falling edge
        .lctrl_mode     = PCNT_MODE_REVERSE,   // reverse when B is low
        .hctrl_mode     = PCNT_MODE_KEEP,      // keep when B is high
        .counter_h_lim  = 32767,
        .counter_l_lim  = -32768,
    };
    pcnt_unit_config(&pcnt_config);

    // Temporarily disable glitch filter while debugging signal
    // pcnt_set_filter_value(PCNT_UNIT_USED, 1000);
    // pcnt_filter_enable(PCNT_UNIT_USED);

    pcnt_counter_pause(PCNT_UNIT_USED);
    pcnt_counter_clear(PCNT_UNIT_USED);
    pcnt_counter_resume(PCNT_UNIT_USED);
}

int encoder_get_delta(void)
{
    static int16_t last = 0;
    static uint32_t last_change_time = 0;
    int16_t now;
    pcnt_get_counter_value(PCNT_UNIT_USED, &now);
    int raw_delta = now - last;

    // With single edge counting, each physical click should be 1 count
    // But add some tolerance for noise/bouncing
    int clicks = 0;
    if (raw_delta >= 1) clicks = 1;
    else if (raw_delta <= -1) clicks = -1;
    
    // Basic debouncing: ignore rapid changes
    uint32_t current_time = esp_timer_get_time() / 1000; // ms
    if (clicks != 0 && (current_time - last_change_time > 100)) { // 100ms debounce
        ESP_LOGI("ENC", "clicks=%d (raw=%d)", clicks, raw_delta);
        last = now;
        last_change_time = current_time;
        
        // reset counters if they drift far to avoid overflow
        if (now > 16000 || now < -16000) {
            pcnt_counter_clear(PCNT_UNIT_USED);
            last = 0;
        }
        return clicks;
    }
    return 0;
} 