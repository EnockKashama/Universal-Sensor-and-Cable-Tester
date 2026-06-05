#ifndef MUX_CONTROL_H
#define MUX_CONTROL_H

#include "driver/gpio.h"
#include "esp_rom_sys.h"

// Multiplexer pins
const uint8_t MUX1_2_S0 = 17;
const uint8_t MUX1_2_S1 = 18;
const uint8_t MUX1_2_S2 = 33;
const uint8_t MUX1_2_S3 = 34;
const uint8_t MUX_EN1 = 15;
const uint8_t MUX_EN2 = 16;
const uint8_t MUX1_2_SIG = 1;

const uint8_t MUX3_4_S0 = 35;
const uint8_t MUX3_4_S1 = 36;
const uint8_t MUX3_4_S2 = 37;
const uint8_t MUX3_4_S3 = 38;
const uint8_t MUX_EN3 = 39;
const uint8_t MUX_EN4 = 40;
const uint8_t MUX3_4_SIG = 2;

#define MUX_SETTLE_TIME_US 250

struct MuxPins {
  uint8_t s0, s1, s2, s3;
  uint8_t en_lo, en_hi;
};

extern const MuxPins outMux;
extern const MuxPins inMux;

inline void mux_gpio_init() {
  const uint8_t output_pins[] = {
      MUX1_2_S0, MUX1_2_S1, MUX1_2_S2, MUX1_2_S3, MUX_EN1, MUX_EN2,   MUX3_4_S0,
      MUX3_4_S1, MUX3_4_S2, MUX3_4_S3, MUX_EN3,   MUX_EN4, MUX1_2_SIG};
  for (auto pin : output_pins) {
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, 0);
  }
  // MUX3_4_SIG is input (ADC) — handled by ADC driver
}

inline void selectMuxChannel(uint8_t channel, const MuxPins &mp) {
  gpio_set_level((gpio_num_t)mp.en_lo, 1);
  gpio_set_level((gpio_num_t)mp.en_hi, 1);

  bool useMux1 = (channel < 16);
  uint8_t localChannel = channel % 16;

  gpio_set_level((gpio_num_t)mp.s0, (localChannel >> 0) & 1);
  gpio_set_level((gpio_num_t)mp.s1, (localChannel >> 1) & 1);
  gpio_set_level((gpio_num_t)mp.s2, (localChannel >> 2) & 1);
  gpio_set_level((gpio_num_t)mp.s3, (localChannel >> 3) & 1);

  gpio_set_level((gpio_num_t)(useMux1 ? mp.en_lo : mp.en_hi), 0);
}

inline void selectOutputChannel(uint8_t channel) {
  selectMuxChannel(channel, outMux);
}

inline void selectInputChannel(uint8_t channel) {
  selectMuxChannel(channel, inMux);
}

inline int getMuxChannel(int pin, int start_offset) {
  if (start_offset == 0)
    return 8 - pin;
  if (start_offset == 1)
    return 7 - pin;
  if (start_offset == 8)
    return pin - 1 + 8;
  return pin - 1 + start_offset;
}

#endif