#ifndef I2C_KEYPAD_H
#define I2C_KEYPAD_H

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
#define I2C_ADDR 0x20
#define I2C_CLK_HZ 100000

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t i2c_dev = NULL;

// Keymap matching your original: "147*2580369#ABCDNF"
static const char keymap[19] = "147*2580369#ABCDNF";

inline esp_err_t i2c_keypad_init() {
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = (gpio_num_t)I2C_SDA_PIN;
  bus_cfg.scl_io_num = (gpio_num_t)I2C_SCL_PIN;
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;

  esp_err_t ret = i2c_new_master_bus(&bus_cfg, &i2c_bus);
  if (ret != ESP_OK)
    return ret;

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = I2C_ADDR;
  dev_cfg.scl_speed_hz = I2C_CLK_HZ;

  return i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev);
}

inline bool i2c_keypad_is_connected() {
  return i2c_master_probe(i2c_bus, I2C_ADDR, 100) == ESP_OK;
}

inline uint8_t i2c_keypad_read_raw() {
  uint8_t write_val = 0xFF;
  i2c_master_transmit(i2c_dev, &write_val, 1, 100);
  vTaskDelay(pdMS_TO_TICKS(1));
  uint8_t data = 0xFF;
  i2c_master_receive(i2c_dev, &data, 1, 100);
  return data;
}

inline char i2c_keypad_get_char() {
  static const char key_map[4][4] = {{'1', '2', '3', 'A'},
                                     {'4', '5', '6', 'B'},
                                     {'7', '8', '9', 'C'},
                                     {'*', '0', '#', 'D'}};

  static const uint8_t col_mask[4] = {0xE, 0xD, 0xB, 0x7};

  for (int row = 0; row < 4; row++) {
    uint8_t row_mask = 0x0F | (~(1 << (row + 4)) & 0xF0);
    i2c_master_transmit(i2c_dev, &row_mask, 1, 100);
    vTaskDelay(pdMS_TO_TICKS(2));
    uint8_t data = 0xFF;
    i2c_master_receive(i2c_dev, &data, 1, 100);
    uint8_t cols = data & 0x0F;
    if (cols != 0x0F) {
      for (int col = 0; col < 4; col++) {
        if (cols == col_mask[col]) {
          return key_map[row][col];
        }
      }
    }
  }
  return 'N';
}
#endif