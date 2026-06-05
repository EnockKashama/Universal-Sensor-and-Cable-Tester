#ifndef TEMP_SENSORS_H
#define TEMP_SENSORS_H

#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mux_control.h"
#include <math.h>

#define TEMP_SENSOR_PIN ADC_CHANNEL_3  // GPIO4
#define PROBE_SENSOR_PIN ADC_CHANNEL_5 // GPIO6
#define SENSOR_A_MUX_CH 31
#define SENSOR_B_TH2_MUX_CH 30
#define DRIVE_SETTLE_MS 10
#define SENSOR_DELTA_PASS_LIMIT 3.0f
#define THERMISTOR_NOMINAL_RES 10000.0f
#define FIXED_RESISTOR 10000.0f
#define REFERENCE_VOLTAGE 3.3f
#define MUX_REFERENCE_VOLTAGE 3.3f
#define ADC_MAX_VALUE 4095.0f
#define STEINHART_A 1.009249522e-03
#define STEINHART_B 2.378405444e-04
#define STEINHART_C 2.019202697e-07
#define MUX_SIG_ADC_CHANNEL ADC_CHANNEL_1 // GPIO2 = MUX3_4_SIG

// ADC handle — initialised in adc_init()
extern adc_oneshot_unit_handle_t adc_handle;

inline void adc_init() {
  adc_oneshot_unit_init_cfg_t init_cfg = {};
  init_cfg.unit_id = ADC_UNIT_1;
  adc_oneshot_new_unit(&init_cfg, &adc_handle);

  adc_oneshot_chan_cfg_t chan_cfg = {};
  chan_cfg.atten = ADC_ATTEN_DB_12;
  chan_cfg.bitwidth = ADC_BITWIDTH_12;
  adc_oneshot_config_channel(adc_handle, TEMP_SENSOR_PIN, &chan_cfg);
  adc_oneshot_config_channel(adc_handle, PROBE_SENSOR_PIN, &chan_cfg);
  adc_oneshot_config_channel(adc_handle, MUX_SIG_ADC_CHANNEL, &chan_cfg);
}

inline int readRawAdc(adc_channel_t channel) {
  int sum = 0;
  int val = 0;
  for (int i = 0; i < 10; i++) {
    adc_oneshot_read(adc_handle, channel, &val);
    sum += val;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return sum / 10;
}

inline float resistanceToCelsius(float r) {
  if (r <= 0.0f || isnan(r) || isinf(r))
    return NAN;
  float ln = logf(r);
  return 1.0f / (STEINHART_A + STEINHART_B * ln + STEINHART_C * ln * ln * ln) -
         273.15f;
}

inline float readThermistorCelsius(adc_channel_t channel, float refVoltage) {
  float v = (readRawAdc(channel) / ADC_MAX_VALUE) * refVoltage;
  if (v <= 0.001f || v >= refVoltage - 0.001f)
    return NAN;
  return resistanceToCelsius(FIXED_RESISTOR * (refVoltage - v) / v);
}

inline float readTemperatureCelsius() {
  return readThermistorCelsius(TEMP_SENSOR_PIN, REFERENCE_VOLTAGE);
}

inline float readTemperatureCelsiusInverted() {
  return readThermistorCelsius(PROBE_SENSOR_PIN, MUX_REFERENCE_VOLTAGE);
}

inline float driveMuxAndSample(uint8_t mux_ch) {
  selectOutputChannel(mux_ch);
  gpio_set_level((gpio_num_t)MUX1_2_SIG, 1);
  vTaskDelay(pdMS_TO_TICKS(DRIVE_SETTLE_MS));
  float t = readTemperatureCelsiusInverted();
  gpio_set_level((gpio_num_t)MUX1_2_SIG, 0);
  gpio_set_level((gpio_num_t)MUX_EN1, 1);
  gpio_set_level((gpio_num_t)MUX_EN2, 1);
  return t;
}

inline float readSensorA() { return driveMuxAndSample(SENSOR_A_MUX_CH); }

inline void readSensorB(float &th1, float &th2) {
  th1 = driveMuxAndSample(SENSOR_A_MUX_CH);
  th2 = driveMuxAndSample(SENSOR_B_TH2_MUX_CH);
}

#endif