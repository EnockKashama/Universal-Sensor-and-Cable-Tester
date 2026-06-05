#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>
#include <stdio.h>
#include <string.h>

// ============================================================
//                  LOVYANGFX DISPLAY CONFIG
// ============================================================
#define PIN_MOSI 11
#define PIN_CLK 12
#define PIN_CS 10
#define PIN_DC 14
#define PIN_RST 13
#define PIN_MISO 42
#define PIN_T_CS 41

struct LGFX_ILI9486 : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9486 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Touch_XPT2046 _touch;

  LGFX_ILI9486() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 20000000;
      cfg.freq_read = 8000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_CLK;
      cfg.pin_mosi = PIN_MOSI;
      cfg.pin_miso = PIN_MISO;
      cfg.pin_dc = PIN_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_CS;
      cfg.pin_rst = PIN_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = true;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = -1;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI2_HOST;
      cfg.freq = 2500000;
      cfg.pin_sclk = PIN_CLK;
      cfg.pin_mosi = PIN_MOSI;
      cfg.pin_miso = PIN_MISO;
      cfg.pin_cs = PIN_T_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

extern LGFX_ILI9486 tft;

// ============================================================
//                    LAYOUT CONSTANTS
// ============================================================
#define HEADER_H 44
#define ACTION_Y 276
#define ACTION_H 44
#define INPUT_Y 230
#define INPUT_H 46
#define CONTENT_Y (HEADER_H + 12)
#define ROW_H 28
#define ROW_GAP 3

// ============================================================
//                    COLOUR PALETTE
// ============================================================
extern uint32_t C_BG;
extern uint32_t C_HEADER;
extern uint32_t C_ACTIONBAR;
extern uint32_t C_ROW;
extern uint32_t C_INPUT_BG;
extern uint32_t C_ROW_PASS;
extern uint32_t C_ROW_FAIL;
extern uint32_t C_ROW_SHORT;
extern uint32_t C_VERDICT_PASS;
extern uint32_t C_VERDICT_FAIL;
extern uint32_t C_TEAL;
extern uint32_t C_RED;
extern uint32_t C_AMBER;
extern uint32_t C_PURPLE;
extern uint32_t C_BLUE;
extern uint32_t C_ORANGE;
extern uint32_t C_YELLOW;
extern uint32_t C_TEXT;
extern uint32_t C_TEXT_DIM;
extern uint32_t C_TEXT_HINT;
extern uint32_t C_DIVIDER;

inline void ui_init_colours() {
  C_BG = 0x1A1A2E;
  C_HEADER = 0x162848;
  C_ACTIONBAR = 0x202630;
  C_ROW = 0x303846;
  C_INPUT_BG = 0x3A4454;
  C_ROW_PASS = 0x185830;
  C_ROW_FAIL = 0x8C1C1C;
  C_ROW_SHORT = 0xA06012;
  C_VERDICT_PASS = 0x146E34;
  C_VERDICT_FAIL = 0xB42020;
  C_TEAL = 0x00FFC8;
  C_RED = 0xFF5A5A;
  C_AMBER = 0xFFC800;
  C_PURPLE = 0xAA78FF;
  C_BLUE = 0x50AAFF;
  C_ORANGE = 0xFF9600;
  C_YELLOW = 0xFFFF15;
  C_TEXT = 0xF5F8FF;
  C_TEXT_DIM = 0xAAB2BE;
  C_TEXT_HINT = 0x788291;
  C_DIVIDER = 0x485466;
}

#endif