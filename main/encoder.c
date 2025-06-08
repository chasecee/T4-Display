#include "encoder.h"
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "esp_log.h"

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

    // Configure first channel: pulse=A, ctrl=B
    pcnt_config_t ch0 = {
        .pulse_gpio_num = ENC_A_GPIO,
        .ctrl_gpio_num  = ENC_B_GPIO,
        .channel        = PCNT_CHANNEL_0,
        .unit           = PCNT_UNIT_USED,
        .pos_mode       = PCNT_COUNT_INC,      // rising edge = +1 when ctrl high
        .neg_mode       = PCNT_COUNT_DEC,      // falling edge = -1 when ctrl high
        .lctrl_mode     = PCNT_MODE_KEEP,
        .hctrl_mode     = PCNT_MODE_REVERSE,   // reverse dir when ctrl high
        .counter_h_lim  = 32767,
        .counter_l_lim  = -32768,
    };
    pcnt_unit_config(&ch0);

    // Second channel: pulse=B ctrl=A so both edges are counted
    pcnt_config_t ch1 = ch0;
    ch1.pulse_gpio_num = ENC_B_GPIO;
    ch1.ctrl_gpio_num  = ENC_A_GPIO;
    ch1.channel        = PCNT_CHANNEL_1;
    pcnt_unit_config(&ch1);

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
    int16_t now;
    pcnt_get_counter_value(PCNT_UNIT_USED, &now);
    int delta = now - last;

    if (delta != 0) {
        ESP_LOGI("ENC", "delta=%d", delta);
        last = now;
        // reset counters if they drift far to avoid overflow
        if (now > 16000 || now < -16000) {
            pcnt_counter_clear(PCNT_UNIT_USED);
            last = 0;
        }
    }
    return delta;
} 