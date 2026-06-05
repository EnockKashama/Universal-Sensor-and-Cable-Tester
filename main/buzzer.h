#ifndef BUZZER_H
#define BUZZER_H

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUZZER_PIN 5
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_LEDC_TIMER LEDC_TIMER_0

#define BUZZER_DURATION_MS 500
#define BUZZER_PASS_FREQ 2000
#define BUZZER_FAIL_FREQ 750
#define BUZZER_FAIL_FREQ2 450

inline void buzzer_init() {
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.timer_num = BUZZER_LEDC_TIMER;
  timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
  timer_cfg.freq_hz = BUZZER_PASS_FREQ;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_cfg);

  ledc_channel_config_t channel_cfg = {};
  channel_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  channel_cfg.channel = BUZZER_LEDC_CHANNEL;
  channel_cfg.timer_sel = BUZZER_LEDC_TIMER;
  channel_cfg.gpio_num = BUZZER_PIN;
  channel_cfg.duty = 0;
  channel_cfg.hpoint = 0;
  ledc_channel_config(&channel_cfg);
}

inline void buzzBuzzer(uint32_t duration_ms = BUZZER_DURATION_MS,
                       uint32_t frequency_hz = BUZZER_PASS_FREQ) {
  ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_TIMER, frequency_hz);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, 128);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
}

#endif