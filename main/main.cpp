#include "buzzer.h"
#include "cable_configs.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "i2c_keypad.h"
#include "memfault/components.h"
#include "memfault/esp_port/core.h"
#include "mux_control.h"
#include "temp_sensors.h"
#include "user_interface.h"
#include "wifi_manager.h"

// ============================================================
//                    HARDWARE INSTANCES
// ============================================================
adc_oneshot_unit_handle_t adc_handle;
const MuxPins outMux{MUX1_2_S0, MUX1_2_S1, MUX1_2_S2,
                     MUX1_2_S3, MUX_EN1,   MUX_EN2};
const MuxPins inMux{MUX3_4_S0, MUX3_4_S1, MUX3_4_S2,
                    MUX3_4_S3, MUX_EN3,   MUX_EN4};
LGFX_ILI9486 tft;

// ============================================================
//                    COLOUR VARIABLES
// ============================================================
uint32_t C_BG, C_HEADER, C_ACTIONBAR, C_ROW, C_INPUT_BG;
uint32_t C_ROW_PASS, C_ROW_FAIL, C_ROW_SHORT;
uint32_t C_VERDICT_PASS, C_VERDICT_FAIL;
uint32_t C_TEAL, C_RED, C_AMBER, C_PURPLE, C_BLUE;
uint32_t C_ORANGE, C_YELLOW, C_TEXT, C_TEXT_DIM, C_TEXT_HINT, C_DIVIDER;

// ============================================================
//                    CABLE CONFIGS
// ============================================================
Mapping configs[] = {
    {1, 8, {1, 4, 5, 6}, {5, 3, 2, 9}, 4, "RAD-SBINV-0001"},
    {1, 0, {6, 3, 2, 1}, {8, 3, 6, 7}, 4, "RAD-SBINV-0002"},
    {1, 0, {5, 4}, {8, 7}, 2, "RJ12 to RJ45 alt"},
    {1, 8, {6, 5, 4}, {1, 2, 3}, 3, "RAD-SBINV-0002"},
    {8, 1, {9, 5, 3, 2}, {6, 1, 5, 4}, 4, "RAD-SBPASS-0001"},
    {0,
     0,
     {1, 2, 3, 4, 5, 6, 7, 8},
     {1, 2, 3, 4, 5, 6, 7, 8},
     8,
     "RJ45 to RJ45"},
    {1, 1, {1, 2, 3, 4, 5, 6}, {1, 2, 3, 4, 5, 6}, 6, "RJ12 to RJ12"},
    {0, 8, {8, 7, 6}, {2, 3, 7}, 3, "RJ45 to DB9"},
};
const int NUM_CONFIGS = sizeof(configs) / sizeof(configs[0]);

// ============================================================
//                    CONSTANTS
// ============================================================
const float CONTINUITY_MIN_VIN = 1.55f;
const float CONTINUITY_MAX_VIN = 3.3f;
const float SHORT_THRESHOLD = 0.5f;
#define WIFI_SETTINGS 1379
float g_lastAmbient = 25.0f;
#define THERMO_HOT_DELTA_MIN 5.0f

// ============================================================
//                    UI HELPERS
// ============================================================
inline int textWidthPx(const char *text, int textSize = 1) {
  return text ? (int)strlen(text) * 6 * textSize : 0;
}

inline void drawHeader(const char *title, float tempC = -999.0f,
                       const char *keyInput = "", uint32_t bgColour = 0) {
  tft.fillRect(0, 0, tft.width(), HEADER_H, bgColour ? bgColour : C_HEADER);
  tft.drawFastHLine(0, HEADER_H, tft.width(), C_DIVIDER);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1.5);
  tft.setCursor(14, 18);
  tft.print(title);
  if (tempC > -999.0f) {
    char tbuf[10];
    snprintf(tbuf, sizeof(tbuf), "%.1fC", tempC);
    int tw = (int)strlen(tbuf) * 9;
    tft.setTextColor(C_TEAL);
    tft.setCursor(tft.width() - tw - 52,
                  18); // shifted left to avoid key indicator
    tft.print(tbuf);
  }
}

inline void drawSectionLabel(const char *label, int y, uint32_t colour = 0) {
  tft.setTextColor(colour ? colour : C_TEXT_HINT);
  tft.setTextSize(1.4);
  tft.setCursor(14, y);
  tft.print(label);
}

inline void drawMenuRow(int x, int y, int w, int h, const char *text,
                        uint32_t accentCol) {
  tft.fillRoundRect(x, y, w, h, 4, C_ROW);
  tft.fillRect(x, y, 4, h, accentCol);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1.8);
  tft.setCursor(x + 12, y + h / 2 - 4);
  tft.print(text);
}

inline void drawActionBar(const char *a = "", const char *b = "",
                          const char *c = "", const char *d = "",
                          uint32_t bgColour = 0) {
  tft.fillRect(0, ACTION_Y, tft.width(), ACTION_H,
               bgColour ? bgColour : C_ACTIONBAR);
  tft.drawFastHLine(0, ACTION_Y, tft.width(), C_DIVIDER);
  const char *lbl[4] = {a, b, c, d};
  int x = 10;
  for (int i = 0; i < 4; i++) {
    if (!lbl[i] || lbl[i][0] == 0)
      continue;
    int w = textWidthPx(lbl[i]) + 14;
    tft.fillRoundRect(x, ACTION_Y + 9, w, 20, 3, C_ROW);
    tft.setTextColor(C_TEXT);
    tft.setTextSize(1.2);
    tft.setCursor(x + 7, ACTION_Y + 17);
    tft.print(lbl[i]);
    x += w + 8;
  }
}

// ============================================================
//                    INPUT HANDLER
// ============================================================
inline void refreshKeyIndicator(float tempC, const char *keyInput) {
  int x = tft.width() - 42;
  tft.fillRect(x, 6, 36, 28, C_HEADER);
  tft.setTextColor(strlen(keyInput) > 0 ? C_TEXT : C_TEXT_HINT);
  tft.setTextSize(1);
  tft.setCursor(x + 3, 18);
  tft.print(">");
  tft.print(strlen(keyInput) > 0 ? keyInput : "_");
}

enum InputMode { NUMERIC, YESNO };
#define INPUT_YES 44
#define INPUT_NO 45
#define INPUT_BACK 46

inline int getInputNumber(InputMode mode = NUMERIC,
                          float ambientTemp = -999.0f) {
  char input[8] = "";
  char lastKey = 0;

  while (true) {
    char key = i2c_keypad_get_char();
    bool newKey = (key != 'N' && key != lastKey);
    lastKey = (key == 'N') ? 0 : key;

    if (newKey) {
      if (mode == NUMERIC) {
        if (key >= '0' && key <= '9' && strlen(input) < sizeof(input) - 1) {
          int len = strlen(input);
          input[len] = key;
          input[len + 1] = '\0';
          refreshKeyIndicator(ambientTemp, input);
        } else if (key == '#' && strlen(input) > 0) {
          int n = atoi(input);
          refreshKeyIndicator(ambientTemp, "");
          return n;
        } else if (key == '*') {
          input[0] = '\0';
          refreshKeyIndicator(ambientTemp, "");
        } else if (key == 'D') {
          refreshKeyIndicator(ambientTemp, "");
          return INPUT_BACK;
        }
      } else {
        if (key == 'A')
          return INPUT_YES;
        if (key == 'B')
          return INPUT_NO;
        if (key == 'D')
          return INPUT_BACK;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

inline void showInputError(const char *msg, float tempC = -999.0f) {
  tft.setTextColor(C_RED);
  tft.setTextSize(1);
  tft.setCursor(14, ACTION_Y - 14);
  tft.print(msg);
  vTaskDelay(pdMS_TO_TICKS(900));
  tft.fillRect(0, ACTION_Y - 20, tft.width(), 20, C_BG);
  refreshKeyIndicator(tempC, "");
}

// ============================================================
//                    CABLE TESTER
// ============================================================
inline void drawResultRow(int y, const char *label, float voltage, bool pass,
                          bool isShort = false) {
  uint32_t bg = isShort ? C_ROW_SHORT : (pass ? C_ROW_PASS : C_ROW_FAIL);
  uint32_t accent = isShort ? C_AMBER : (pass ? C_TEAL : C_RED);
  tft.fillRoundRect(12, y, 344, ROW_H - 2, 3, bg);
  tft.setTextColor(accent);
  tft.setTextSize(1);
  tft.setCursor(22, y + 9);
  tft.print(label);
  char vbuf[10];
  snprintf(vbuf, sizeof(vbuf), "%.2fV", voltage);
  tft.setCursor(230, y + 9);
  tft.print(vbuf);
  const char *badge = isShort ? "SHORT" : (pass ? "PASS" : "FAIL");
  int bw = textWidthPx(badge) + 10;
  tft.fillRoundRect(356 - bw, y + 5, bw, 16, 3, bg);
  tft.setTextColor(accent);
  tft.setTextSize(1);
  tft.setCursor(356 - bw + 5, y + 9);
  tft.print(badge);
}

inline void drawVerdictCard(bool pass, int failCount, int shortCount,
                            int total) {
  int cx = 366, cy = CONTENT_Y, cw = 102, ch = 95;
  tft.fillRoundRect(cx, cy, cw, ch, 6, pass ? C_VERDICT_PASS : C_VERDICT_FAIL);
  tft.drawFastHLine(cx, cy, cw, pass ? C_TEAL : C_RED);
  tft.setTextColor(pass ? C_TEAL : C_RED);
  tft.setTextSize(3);
  const char *v = pass ? "PASS" : "FAIL";
  int tw = textWidthPx(v, 3);
  tft.setCursor(cx + (cw - tw) / 2, cy + 24);
  tft.print(v);
  tft.setTextSize(1);
  char sub[24];
  if (pass)
    snprintf(sub, sizeof(sub), "ALL %d OK", total);
  else
    snprintf(sub, sizeof(sub), "%dFAIL %dSHORT", failCount, shortCount);
  int sw = textWidthPx(sub);
  tft.setCursor(cx + (cw - sw) / 2, cy + 72);
  tft.print(sub);
}

void runCableTest(int c) {
  bool retest = true;
  while (retest) {
    g_lastAmbient = readTemperatureCelsius();
    printf("SESSION,%s,%lu\n", configs[c].config_name,
           (unsigned long)xTaskGetTickCount());

    tft.fillScreen(C_BG);
    char hdr[56];
    snprintf(hdr, sizeof(hdr), "TESTING  %s", configs[c].config_name);
    drawHeader(hdr, g_lastAmbient);
    drawSectionLabel("PAIR            VOLTAGE   RESULT", CONTENT_Y);

    bool pass = true;
    int failCount = 0, shortCount = 0;

    for (int i = 0; i < configs[c].length; i++) {
      int chA = getMuxChannel(configs[c].mapA[i], configs[c].endA_mux_start);
      int chB = getMuxChannel(configs[c].mapB[i], configs[c].endB_mux_start);

      selectOutputChannel(chA);
      gpio_set_level((gpio_num_t)MUX1_2_SIG, 1);
      esp_rom_delay_us(MUX_SETTLE_TIME_US);
      selectInputChannel(chB);
      esp_rom_delay_us(MUX_SETTLE_TIME_US);

      int raw = 0;
      adc_oneshot_read(adc_handle, MUX_SIG_ADC_CHANNEL, &raw);
      float voltage = (raw / ADC_MAX_VALUE) * 3.3f;
      bool pairOk =
          (voltage >= CONTINUITY_MIN_VIN && voltage <= CONTINUITY_MAX_VIN);
      if (!pairOk) {
        pass = false;
        failCount++;
      }

      char label[20];
      snprintf(label, sizeof(label), "A%d -> B%d", configs[c].mapA[i],
               configs[c].mapB[i]);
      drawResultRow(CONTENT_Y + 14 + i * (ROW_H + ROW_GAP), label, voltage,
                    pairOk, false);
      vTaskDelay(pdMS_TO_TICKS(200));

      printf("RESULT,%s,A%d,B%d,%.3f,%s\n", configs[c].config_name,
             configs[c].mapA[i], configs[c].mapB[i], voltage,
             pairOk ? "PASS" : "FAIL");

      // Short detection
      for (int j = 0; j < configs[c].length; j++) {
        if (j == i)
          continue;
        selectInputChannel(
            getMuxChannel(configs[c].mapB[j], configs[c].endB_mux_start));
        esp_rom_delay_us(MUX_SETTLE_TIME_US);
        adc_oneshot_read(adc_handle, MUX_SIG_ADC_CHANNEL, &raw);
        voltage = (raw / ADC_MAX_VALUE) * 3.3f;
        if (voltage > SHORT_THRESHOLD) {
          char sl[24];
          snprintf(sl, sizeof(sl), "SHORT A%d->B%d", configs[c].mapA[i],
                   configs[c].mapB[j]);
          int sy = CONTENT_Y + 14 +
                   (configs[c].length + shortCount) * (ROW_H + ROW_GAP);
          drawResultRow(sy, sl, voltage, false, true);
          pass = false;
          shortCount++;
          printf("SHORT,%s,A%d,B%d,%.3f\n", configs[c].config_name,
                 configs[c].mapA[i], configs[c].mapB[j], voltage);
        }
      }
      gpio_set_level((gpio_num_t)MUX1_2_SIG, 0);
    }

    drawVerdictCard(pass, failCount, shortCount, configs[c].length);

    if (pass) {
      buzzBuzzer(BUZZER_DURATION_MS, BUZZER_PASS_FREQ);
    } else {
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ);
      vTaskDelay(pdMS_TO_TICKS(100));
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ2);
    }

    printf("OVERALL,%s,%s,%.1f\n", configs[c].config_name,
           pass ? "PASS" : "FAIL", g_lastAmbient);

    drawActionBar("A  Retest", "B  Main menu");

    while (true) {
      int k = getInputNumber(YESNO, g_lastAmbient);
      if (k == INPUT_YES) {
        retest = true;
        tft.fillScreen(C_BG);
        break;
      }
      if (k == INPUT_NO || k == INPUT_BACK) {
        retest = false;
        break;
      }
    }
  }
}

// ============================================================
//                    SENSOR TEST
// ============================================================
inline void drawTempCard(int x, int y, int w, int h, const char *label,
                         float tempC, float deltaC, bool showDelta,
                         bool pass = true) {
  uint32_t accent = showDelta ? (pass ? C_TEAL : C_RED) : C_PURPLE;
  uint32_t bg = showDelta ? (pass ? C_ROW_PASS : C_ROW_FAIL) : C_ROW;
  tft.fillRoundRect(x, y, w, h, 5, bg);
  tft.drawFastHLine(x, y, w, accent);
  tft.setTextColor(C_TEXT_DIM);
  tft.setTextSize(1);
  int lw = textWidthPx(label);
  tft.setCursor(x + (w - lw) / 2, y + 10);
  tft.print(label);
  char buf[12];
  snprintf(buf, sizeof(buf), isnan(tempC) ? "--.-C" : "%.1fC", tempC);
  tft.setTextColor(showDelta ? accent : C_TEXT);
  tft.setTextSize(2);
  int tw = textWidthPx(buf, 2);
  tft.setCursor(x + (w - tw) / 2, y + h / 2 - 4);
  tft.print(buf);
  if (showDelta) {
    char db[12];
    snprintf(db, sizeof(db), isnan(deltaC) ? "d--.-" : "d%.1f", deltaC);
    int dw = textWidthPx(db) + 8;
    tft.fillRoundRect(x + (w - dw) / 2, y + h - 20, dw, 14, 3,
                      pass ? C_ROW_PASS : C_ROW_FAIL);
    tft.setTextColor(accent);
    tft.setTextSize(1);
    tft.setCursor(x + (w - dw) / 2 + 4, y + h - 16);
    tft.print(db);
  }
}

inline void drawSensorVerdict(int vy, bool pass, const char *hint = nullptr) {
  tft.fillRoundRect(12, vy, 456, 52, 5, pass ? C_VERDICT_PASS : C_VERDICT_FAIL);
  tft.drawFastHLine(12, vy, 456, pass ? C_TEAL : C_RED);
  tft.setTextColor(pass ? C_TEAL : C_RED);
  tft.setTextSize(3);
  const char *verdict = pass ? "PASS" : "FAIL";
  int vw = strlen(verdict) * 18;
  tft.setCursor((tft.width() - vw) / 2, vy + 14);
  tft.print(verdict);
  if (hint) {
    tft.setTextColor(C_TEXT_DIM);
    tft.setTextSize(1);
    tft.setCursor((tft.width() - (int)strlen(hint) * 6) / 2, vy + 40);
    tft.print(hint);
  }
}

void runOutletSensorLoop() {
  bool retest = true;
  while (retest) {
    float ambient = readTemperatureCelsius();
    float measured = readSensorA();
    float delta = measured - ambient;
    bool pass = !isnan(ambient) && !isnan(measured) &&
                fabsf(delta) <= SENSOR_DELTA_PASS_LIMIT;

    tft.fillScreen(C_BG);
    drawHeader("SENSOR TEST  -  OUTLET", ambient, "", 0x3A1A1A);
    drawSectionLabel("AMBIENT vs OUTLET SENSOR", CONTENT_Y);

    drawTempCard(12, CONTENT_Y + 14, 220, 88, "AMBIENT", ambient, 0, false);
    drawTempCard(244, CONTENT_Y + 14, 222, 88, "OUTLET", measured, delta, true,
                 pass);

    int vy = CONTENT_Y + 114;
    const char *hint = (isnan(ambient) || isnan(measured))
                           ? "Sensor reading invalid"
                           : nullptr;
    drawSensorVerdict(vy, pass, hint);

    if (pass)
      buzzBuzzer(BUZZER_DURATION_MS, BUZZER_PASS_FREQ);
    else {
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ);
      vTaskDelay(pdMS_TO_TICKS(100));
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ2);
    }

    printf("SENSOR,Outlet,%.1f,%.1f,%.1f,%s\n", ambient, measured, delta,
           pass ? "PASS" : "FAIL");

    drawActionBar("A  Retest", "B  Back", "", "", 0x3A1A1A);
    while (true) {
      int k = getInputNumber(YESNO, ambient);
      if (k == INPUT_YES) {
        retest = true;
        break;
      }
      if (k == INPUT_NO || k == INPUT_BACK) {
        retest = false;
        break;
      }
    }
  }
}

void runInletSensorLoop() {
  bool retest = true;
  while (retest) {
    float ambient = readTemperatureCelsius();
    float th1 = 0.0f, th2 = 0.0f;
    readSensorB(th1, th2);
    float d1 = th1 - ambient;
    float d2 = th2 - ambient;
    bool p1 =
        !isnan(ambient) && !isnan(th1) && fabsf(d1) <= SENSOR_DELTA_PASS_LIMIT;
    bool p2 =
        !isnan(ambient) && !isnan(th2) && fabsf(d2) <= SENSOR_DELTA_PASS_LIMIT;
    bool pass = p1 && p2;

    tft.fillScreen(C_BG);
    drawHeader("SENSOR TEST  -  INLET", ambient, "", 0x1A3A5C);
    drawSectionLabel("AMBIENT  /  Th1  /  Th2", CONTENT_Y);

    drawTempCard(12, CONTENT_Y + 14, 148, 88, "AMBIENT", ambient, 0, false);
    drawTempCard(170, CONTENT_Y + 14, 148, 88, "Th1", th1, d1, true, p1);
    drawTempCard(328, CONTENT_Y + 14, 148, 88, "Th2", th2, d2, true, p2);

    int vy = CONTENT_Y + 114;
    const char *hint = nullptr;
    if (!pass) {
      if (isnan(ambient) || isnan(th1) || isnan(th2))
        hint = "Sensor reading invalid";
      else if (!p1 && !p2)
        hint = "Both Th1 and Th2 out of range";
      else if (!p1)
        hint = "Check Th1 probe connection";
      else
        hint = "Check Th2 probe connection";
    }
    drawSensorVerdict(vy, pass, hint);

    if (pass)
      buzzBuzzer(BUZZER_DURATION_MS, BUZZER_PASS_FREQ);
    else {
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ);
      vTaskDelay(pdMS_TO_TICKS(100));
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ2);
    }

    printf("SENSOR,Inlet Th1,%.1f,%.1f,%.1f,%s\n", ambient, th1, d1,
           p1 ? "PASS" : "FAIL");
    printf("SENSOR,Inlet Th2,%.1f,%.1f,%.1f,%s\n", ambient, th2, d2,
           p2 ? "PASS" : "FAIL");

    drawActionBar("A  Retest", "B  Back", "", "", 0x1A3A5C);
    while (true) {
      int k = getInputNumber(YESNO, ambient);
      if (k == INPUT_YES) {
        retest = true;
        break;
      }
      if (k == INPUT_NO || k == INPUT_BACK) {
        retest = false;
        break;
      }
    }
  }
}

void runThermostatLoop() {
  static constexpr uint32_t ACCENT = 0x3A3A10;
  bool doPhase2 = false;
  float baselineTemp = NAN;

  // ── PHASE 1: Ambient check ────────────────────────────────
  bool retestPhase1 = true;
  while (retestPhase1) {
    float ambient = readTemperatureCelsius();
    float measured = readSensorA();
    float delta = measured - ambient;
    bool pass = !isnan(ambient) && !isnan(measured) &&
                fabsf(delta) <= SENSOR_DELTA_PASS_LIMIT;

    tft.fillScreen(C_BG);
    drawHeader("SENSOR TEST  -  THERMOSTAT", ambient, "", ACCENT);
    drawSectionLabel("PHASE 1  -  AMBIENT CHECK", CONTENT_Y);

    drawTempCard(12, CONTENT_Y + 14, 220, 88, "AMBIENT", ambient, 0, false);
    drawTempCard(244, CONTENT_Y + 14, 222, 88, "THERMOSTAT", measured, delta,
                 true, pass);

    int vy = CONTENT_Y + 114;
    const char *hint = (isnan(ambient) || isnan(measured))
                           ? "Sensor reading invalid"
                           : nullptr;
    drawSensorVerdict(vy, pass, hint);

    if (pass) {
      buzzBuzzer(BUZZER_DURATION_MS, BUZZER_PASS_FREQ);
      baselineTemp = measured;
      drawActionBar("A  Hot Water Test", "B  Back", "", "", ACCENT);
    } else {
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ);
      vTaskDelay(pdMS_TO_TICKS(100));
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ2);
      drawActionBar("A  Retest", "B  Back", "", "", ACCENT);
    }

    printf("SENSOR,Thermostat Ph1,%.1f,%.1f,%.1f,%s\n", ambient, measured,
           delta, pass ? "PASS" : "FAIL");

    while (true) {
      int k = getInputNumber(YESNO, ambient);
      if (k == INPUT_YES) {
        if (pass) {
          doPhase2 = true;
          retestPhase1 = false;
        } else {
          retestPhase1 = true;
        }
        break;
      }
      if (k == INPUT_NO || k == INPUT_BACK) {
        retestPhase1 = false;
        doPhase2 = false;
        break;
      }
    }
  }

  if (!doPhase2)
    return;

  // ── Instruction screen ────────────────────────────────────
  tft.fillScreen(C_BG);
  drawHeader("SENSOR TEST  -  THERMOSTAT", -999.0f, "", ACCENT);
  drawSectionLabel("PHASE 2  -  HOT WATER TEST", CONTENT_Y);

  tft.fillRoundRect(12, CONTENT_Y + 14, 456, 110, 6, C_ROW);
  tft.drawFastHLine(12, CONTENT_Y + 14, 456, C_AMBER);
  tft.setTextColor(C_AMBER);
  tft.setTextSize(2);
  tft.setCursor(22, CONTENT_Y + 26);
  tft.print("Submerge thermostat probe");
  tft.setCursor(22, CONTENT_Y + 48);
  tft.print("in hot water, then press");
  tft.setTextColor(C_TEXT);
  tft.setCursor(22, CONTENT_Y + 70);
  tft.print("A  to begin measurement.");
  tft.setTextColor(C_TEXT_DIM);
  tft.setTextSize(1);
  char blbuf[32];
  snprintf(blbuf, sizeof(blbuf), "Baseline: %.1f C", baselineTemp);
  tft.setCursor(22, CONTENT_Y + 100);
  tft.print(blbuf);

  drawActionBar("A  Start", "B  Back", "", "", ACCENT);

  bool proceedHot = false;
  while (true) {
    int k = getInputNumber(YESNO, -999.0f);
    if (k == INPUT_YES) {
      proceedHot = true;
      break;
    }
    if (k == INPUT_NO || k == INPUT_BACK)
      break;
  }
  if (!proceedHot)
    return;

  // ── PHASE 2: Hot water rising test ───────────────────────
  bool retestPhase2 = true;
  while (retestPhase2) {
    float ambient = readTemperatureCelsius();
    float hot = readSensorA();
    float rise = hot - baselineTemp;
    bool pass = !isnan(hot) && rise >= THERMO_HOT_DELTA_MIN;

    tft.fillScreen(C_BG);
    drawHeader("SENSOR TEST  -  THERMOSTAT", ambient, "", ACCENT);
    drawSectionLabel("PHASE 2  -  HOT WATER TEST", CONTENT_Y);

    drawTempCard(12, CONTENT_Y + 14, 220, 88, "BASELINE", baselineTemp, 0,
                 false);
    drawTempCard(244, CONTENT_Y + 14, 222, 88, "HOT", hot, rise, true, pass);

    // Rise indicator
    int ry = CONTENT_Y + 114;
    tft.fillRoundRect(12, ry, 456, 28, 4, pass ? C_ROW_PASS : C_ROW_FAIL);
    tft.setTextColor(pass ? C_TEAL : C_RED);
    tft.setTextSize(1);
    char rbuf[48];
    snprintf(rbuf, sizeof(rbuf),
             "Rise: %.1f C   (need >= %.0f C above baseline)", rise,
             THERMO_HOT_DELTA_MIN);
    tft.setCursor(20, ry + 9);
    tft.print(rbuf);

    // Verdict
    int vy = ry + 36;
    tft.fillRoundRect(12, vy, 456, 44, 5,
                      pass ? C_VERDICT_PASS : C_VERDICT_FAIL);
    tft.drawFastHLine(12, vy, 456, pass ? C_TEAL : C_RED);
    tft.setTextColor(pass ? C_TEAL : C_RED);
    tft.setTextSize(3);
    const char *verdict = pass ? "PASS" : "FAIL";
    tft.setCursor((tft.width() - (int)strlen(verdict) * 18) / 2, vy + 10);
    tft.print(verdict);

    if (pass)
      buzzBuzzer(BUZZER_DURATION_MS, BUZZER_PASS_FREQ);
    else {
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ);
      vTaskDelay(pdMS_TO_TICKS(100));
      buzzBuzzer(BUZZER_DURATION_MS / 2, BUZZER_FAIL_FREQ2);
    }

    printf("SENSOR,Thermostat Ph2,%.1f,%.1f,%.1f,%s\n", baselineTemp, hot, rise,
           pass ? "PASS" : "FAIL");

    drawActionBar("A  Retest", "B  Back", "", "", ACCENT);
    while (true) {
      int k = getInputNumber(YESNO, ambient);
      if (k == INPUT_YES) {
        retestPhase2 = true;
        break;
      }
      if (k == INPUT_NO || k == INPUT_BACK) {
        retestPhase2 = false;
        break;
      }
    }
  }
}

void runSensorTest() {
  while (true) {
    tft.fillScreen(C_BG);
    drawHeader("PLENTIFY  -  SENSOR TEST");
    drawSectionLabel("SELECT SENSOR", CONTENT_Y);

    drawMenuRow(12, CONTENT_Y + 14, 224, ROW_H, "1  Outlet Sensor", C_RED);
    drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP), 224, ROW_H,
                "2  Inlet Sensor", C_TEAL);
    drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP) * 2, 224, ROW_H,
                "3  Thermostat", C_YELLOW);

    drawActionBar("# Confirm", "* Clear", "D  Back");
    refreshKeyIndicator(-999.0f, "");

    int sel = getInputNumber(NUMERIC, -999.0f);
    if (sel == INPUT_BACK || sel == 0)
      return;

    if (sel == 1)
      runOutletSensorLoop();
    else if (sel == 2)
      runInletSensorLoop();
    else if (sel == 3)
      runThermostatLoop();
    else
      showInputError("1, 2 or 3 only");
  }
}
// ============================================================
//                    PIN MAPPER
// ============================================================
// ============================================================
//                    PIN MAPPER
// ============================================================
inline const char *getPinName(int ch, char *buf, int buflen) {
  if (ch >= 0 && ch <= 7) {
    int r = 8 - ch;
    if (ch >= 1 && ch <= 6)
      snprintf(buf, buflen, "RJ45_%d/RJ12_%d", r, 7 - ch);
    else
      snprintf(buf, buflen, "RJ45_%d", r);
  } else if (ch >= 8 && ch <= 16) {
    snprintf(buf, buflen, "DB9_%d", ch - 7);
  } else {
    snprintf(buf, buflen, "CH_%d", ch);
  }
  return buf;
}

inline const char *getCompactPinName(int ch, char *buf, int buflen) {
  if (ch >= 0 && ch <= 7) {
    int r = 8 - ch;
    if (ch >= 1 && ch <= 6)
      snprintf(buf, buflen, "R45.%d/12.%dC%d", r, 7 - ch, ch);
    else
      snprintf(buf, buflen, "R45.%dC%d", r, ch);
  } else if (ch >= 8 && ch <= 16) {
    snprintf(buf, buflen, "DB9.%dC%d", ch - 7, ch);
  } else {
    snprintf(buf, buflen, "C%d", ch);
  }
  return buf;
}

void runPinMapper() {
  while (true) {
    tft.fillScreen(C_BG);
    drawHeader("PLENTIFY  -  PIN MAPPER");
    tft.setTextColor(C_PURPLE);
    tft.setTextSize(2);
    tft.setCursor((tft.width() - 144) / 2, 130);
    tft.print("Scanning...");

    struct Conn {
      int a, b;
    };
    Conn connections[50];
    int count = 0;

    for (int chA = 0; chA <= 16; chA++) {
      selectOutputChannel(chA);
      gpio_set_level((gpio_num_t)MUX1_2_SIG, 1);
      esp_rom_delay_us(MUX_SETTLE_TIME_US);
      for (int chB = 0; chB <= 16; chB++) {
        selectInputChannel(chB);
        esp_rom_delay_us(MUX_SETTLE_TIME_US);
        int raw = 0;
        adc_oneshot_read(adc_handle, MUX_SIG_ADC_CHANNEL, &raw);
        float v = (raw / ADC_MAX_VALUE) * 3.3f;
        if (v >= CONTINUITY_MIN_VIN && count < 50) {
          connections[count++] = {chA, chB};
        }
      }
      gpio_set_level((gpio_num_t)MUX1_2_SIG, 0);
    }

    tft.fillScreen(C_BG);
    drawHeader("PLENTIFY  -  PIN MAPPER");
    drawSectionLabel("DETECTED CONNECTIONS", CONTENT_Y);

    if (count == 0) {
      tft.setTextColor(C_TEXT_DIM);
      tft.setTextSize(2);
      tft.setCursor((tft.width() - 180) / 2, 130);
      tft.print("No connections");
    } else {
      const int MAX_DISPLAY = 12;
      const int ROWS_PER_COL = 6;
      const int startY = CONTENT_Y + 14;
      const int colX[2] = {12, 248};
      const int colW[2] = {228, 220};

      int displayed = count < MAX_DISPLAY ? count : MAX_DISPLAY;
      char nameBuf1[24], nameBuf2[24], lineBuf[52];

      for (int i = 0; i < displayed; i++) {
        char a[16], b[16];
        getCompactPinName(connections[i].a, a, sizeof(a));
        getCompactPinName(connections[i].b, b, sizeof(b));
        snprintf(lineBuf, sizeof(lineBuf), "%s->%s", a, b);

        printf("MAP_DETECTION,%s->%s\n",
               getPinName(connections[i].a, nameBuf1, sizeof(nameBuf1)),
               getPinName(connections[i].b, nameBuf2, sizeof(nameBuf2)));

        int col = i / ROWS_PER_COL;
        int row = i % ROWS_PER_COL;
        int rx = colX[col];
        int ry = startY + row * (ROW_H + ROW_GAP);
        drawMenuRow(rx, ry, colW[col], ROW_H, lineBuf, C_TEAL);
      }

      for (int i = MAX_DISPLAY; i < count; i++) {
        printf("MAP_DETECTION,%s->%s\n",
               getPinName(connections[i].a, nameBuf1, sizeof(nameBuf1)),
               getPinName(connections[i].b, nameBuf2, sizeof(nameBuf2)));
      }

      if (count > MAX_DISPLAY) {
        tft.setTextColor(C_AMBER);
        tft.setTextSize(1);
        char more[32];
        snprintf(more, sizeof(more), "+ %d more - see Serial",
                 count - MAX_DISPLAY);
        tft.setCursor(12, startY + ROWS_PER_COL * (ROW_H + ROW_GAP) + 2);
        tft.print(more);
      }
    }

    buzzBuzzer(200, count > 0 ? BUZZER_PASS_FREQ : BUZZER_FAIL_FREQ);
    drawActionBar("A  Rescan", "D  Back");

    while (true) {
      int k = getInputNumber(YESNO);
      if (k == INPUT_YES)
        break;
      if (k == INPUT_BACK)
        return;
    }
  }
}
// ============================================================
//                    WIFI SETTINGS
// ============================================================
void runWiFiSettings() {
  while (true) {
    tft.fillScreen(C_BG);
    drawHeader("PLENTIFY  -  WIFI SETTINGS");
    drawSectionLabel("SELECT OPTION", CONTENT_Y);

    drawMenuRow(12, CONTENT_Y + 14, 300, ROW_H, "1  Connect WiFi", C_TEAL);
    drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP), 300, ROW_H,
                "2  Start Config Portal", C_PURPLE);
    drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP) * 2, 300, ROW_H,
                "3  WiFi Status", C_BLUE);
    drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP) * 3, 300, ROW_H,
                "4  Disconnect / Clear", C_AMBER);

    drawActionBar("# Confirm", "* Clear", "D  Back");
    refreshKeyIndicator(-999.0f, "");

    int sel = getInputNumber(NUMERIC, -999.0f);
    if (sel == INPUT_BACK || sel == 0)
      return;

    if (sel == 1) {
      tft.fillScreen(C_BG);
      drawHeader("WIFI  -  CONNECTING");
      tft.setTextColor(C_TEAL);
      tft.setTextSize(2);
      tft.setCursor(14, 130);
      tft.print("Trying saved credentials...");

      if (!s_wifi_event_group)
        s_wifi_event_group = xEventGroupCreate();
      esp_wifi_connect();

      EventBits_t bits = xEventGroupWaitBits(
          s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE,
          pdFALSE, pdMS_TO_TICKS(10000));

      tft.fillScreen(C_BG);
      if (bits & WIFI_CONNECTED_BIT) {
        drawHeader("WIFI  -  CONNECTED");
        tft.setTextColor(C_TEAL);
        tft.setTextSize(2);
        tft.setCursor(14, CONTENT_Y + 20);
        tft.print("Connected!");
        buzzBuzzer(300, BUZZER_PASS_FREQ);
      } else {
        drawHeader("WIFI  -  FAILED");
        tft.setTextColor(C_RED);
        tft.setTextSize(2);
        tft.setCursor(14, CONTENT_Y + 30);
        tft.print("No saved credentials.");
        tft.setTextColor(C_TEXT_DIM);
        tft.setTextSize(1);
        tft.setCursor(14, CONTENT_Y + 70);
        tft.print("Use option 2 to set up WiFi.");
        buzzBuzzer(300, BUZZER_FAIL_FREQ);
      }
      drawActionBar("D  Back");
      getInputNumber(YESNO);

    } else if (sel == 2) {
      // Config portal
      tft.fillScreen(C_BG);
      drawHeader("WIFI  -  CONFIG PORTAL");
      drawSectionLabel("AP CONFIG PORTAL ACTIVE", CONTENT_Y);

      wifi_start_portal();

      tft.setTextColor(C_TEAL);
      tft.setTextSize(2);
      tft.setCursor(14, CONTENT_Y + 20);
      tft.print("SSID: " PORTAL_AP_SSID);
      tft.setTextColor(C_TEXT);
      tft.setTextSize(1);
      tft.setCursor(14, CONTENT_Y + 60);
      tft.print("Connect your phone to this WiFi");
      tft.setCursor(14, CONTENT_Y + 76);
      tft.print("then open browser and go to:");
      tft.setTextColor(C_TEAL);
      tft.setTextSize(2);
      tft.setCursor(14, CONTENT_Y + 96);
      tft.print("192.168.4.1");

      drawActionBar("D  Cancel");

      // Wait for credentials or cancel
      while (wifi_portal_running()) {
        char k = i2c_keypad_get_char();
        if (k == 'D')
          break;
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      wifi_stop_portal();

      if (wifi_portal_got_credentials()) {
        tft.fillScreen(C_BG);
        drawHeader("WIFI  -  CONNECTING");
        tft.setTextColor(C_TEAL);
        tft.setTextSize(2);
        tft.setCursor(14, 130);
        char msg[96];
        snprintf(msg, sizeof(msg), "Connecting to: %s", wifi_portal_ssid());
        tft.print(msg);

        esp_err_t result =
            wifi_connect_sta(wifi_portal_ssid(), wifi_portal_pass());
        tft.fillScreen(C_BG);
        if (result == ESP_OK) {
          drawHeader("WIFI  -  CONNECTED");
          tft.setTextColor(C_TEAL);
          tft.setTextSize(2);
          tft.setCursor(14, CONTENT_Y + 20);
          tft.print("Connected!");
          tft.setTextColor(C_TEXT);
          tft.setCursor(14, CONTENT_Y + 60);
          tft.print(wifi_portal_ssid());
          buzzBuzzer(300, BUZZER_PASS_FREQ);
        } else {
          drawHeader("WIFI  -  FAILED");
          tft.setTextColor(C_RED);
          tft.setTextSize(2);
          tft.setCursor(14, CONTENT_Y + 30);
          tft.print("Connection Failed");
          tft.setTextColor(C_TEXT_DIM);
          tft.setTextSize(1);
          tft.setCursor(14, CONTENT_Y + 70);
          tft.print("Check SSID and password.");
          buzzBuzzer(300, BUZZER_FAIL_FREQ);
        }
      } else {
        tft.fillScreen(C_BG);
        drawHeader("WIFI  -  CANCELLED");
        tft.setTextColor(C_RED);
        tft.setTextSize(2);
        tft.setCursor(14, 130);
        tft.print("Portal Cancelled");
        buzzBuzzer(200, BUZZER_FAIL_FREQ);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      drawActionBar("D  Back");
      getInputNumber(YESNO);

    } else if (sel == 3) {
      tft.fillScreen(C_BG);
      drawHeader("WIFI  -  STATUS");
      drawSectionLabel("CURRENT WIFI STATUS", CONTENT_Y);

      if (wifi_is_connected()) {
        drawMenuRow(12, CONTENT_Y + 14, 440, ROW_H, "Status : Connected",
                    C_TEAL);
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
          char ipbuf[48];
          snprintf(ipbuf, sizeof(ipbuf), "IP: " IPSTR, IP2STR(&ip_info.ip));
          drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP), 440, ROW_H, ipbuf,
                      C_TEXT);
        }
        drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP) * 2, 440, ROW_H,
                    "Memfault: Upload active", C_TEAL);
      } else {
        drawMenuRow(12, CONTENT_Y + 14, 440, ROW_H, "Status : Disconnected",
                    C_RED);
        drawMenuRow(12, CONTENT_Y + 14 + (ROW_H + ROW_GAP), 440, ROW_H,
                    "Memfault: Offline", C_TEXT_DIM);
      }
      drawActionBar("D  Back");
      getInputNumber(YESNO);

    } else if (sel == 4) {
      tft.fillScreen(C_BG);
      drawHeader("WIFI  -  RESET");
      tft.setTextColor(C_AMBER);
      tft.setTextSize(2);
      tft.setCursor(14, 130);
      tft.print("Clearing WiFi settings...");
      esp_wifi_disconnect();
      esp_wifi_stop();
      esp_wifi_restore();
      esp_wifi_start();
      vTaskDelay(pdMS_TO_TICKS(500));
      tft.setTextColor(C_TEAL);
      tft.setCursor(14, 160);
      tft.print("WiFi Reset Done!");
      buzzBuzzer(300, BUZZER_PASS_FREQ);
      vTaskDelay(pdMS_TO_TICKS(1500));
    } else {
      showInputError("1 - 4 only");
    }
  }
}
// ============================================================
//                    MAIN MENU
// ============================================================
void runMainMenu() {
  while (true) {
    g_lastAmbient = readTemperatureCelsius();

    tft.fillScreen(C_BG);
    drawHeader("PLENTIFY  -  CABLE TESTER  V1.0.0", g_lastAmbient);
    drawSectionLabel("SELECT AN OPTION", CONTENT_Y, C_ORANGE);

    // Left column — first 5 configs
    for (int i = 0; i < 5 && i < NUM_CONFIGS; i++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%d  %s", i + 1, configs[i].config_name);
      drawMenuRow(12, CONTENT_Y + 14 + i * (ROW_H + ROW_GAP), 224, ROW_H, buf,
                  C_PURPLE);
    }
    // Right column — remaining configs
    for (int i = 5; i < NUM_CONFIGS; i++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%d  %s", i + 1, configs[i].config_name);
      drawMenuRow(248, CONTENT_Y + 14 + (i - 5) * (ROW_H + ROW_GAP), 220, ROW_H,
                  buf, C_PURPLE);
    }

    // Sensor and pin mapper buttons
    int ry = CONTENT_Y + 14 + (NUM_CONFIGS - 5) * (ROW_H + ROW_GAP);
    char sBuf[24], mBuf[24];
    snprintf(sBuf, sizeof(sBuf), "%d  Sensor Test", NUM_CONFIGS + 1);
    snprintf(mBuf, sizeof(mBuf), "%d  Pin Mapper", NUM_CONFIGS + 2);
    drawMenuRow(248, ry, 220, ROW_H, sBuf, C_TEAL);
    drawMenuRow(248, ry + (ROW_H + ROW_GAP), 220, ROW_H, mBuf, C_BLUE);

    drawActionBar("# Confirm", "* Clear");
    refreshKeyIndicator(g_lastAmbient, "");

    int sel = getInputNumber(NUMERIC, g_lastAmbient);

    if (sel == INPUT_BACK)
      continue;

    if (sel >= 1 && sel <= NUM_CONFIGS) {
      runCableTest(sel - 1);
    } else if (sel == NUM_CONFIGS + 1) {
      runSensorTest();
    } else if (sel == NUM_CONFIGS + 2) {
      runPinMapper();
    } else if (sel == WIFI_SETTINGS) {
      runWiFiSettings();
    } else {
      showInputError("Invalid selection", g_lastAmbient);
    }
  }
}
// ============================================================
//                    PASSWORD SCREEN
// ============================================================
#define PASSWORD "1234"

void runPasswordScreen() {
  tft.fillScreen(0x3A2A7A);

  // Same header as splash
  tft.setTextColor(0xFFFFFF);
  tft.setTextSize(4);
  const char *brand = "plentify";
  int brandW = strlen(brand) * 24;
  int brandX = (tft.width() - brandW) / 2;
  int brandY = 80;
  tft.setCursor(brandX, brandY);
  tft.print(brand);

  // Teal dot over 'i'
  int dotX = brandX + 5 * 24 + 12;
  int dotY = brandY - 8;
  tft.fillCircle(dotX, dotY, 7, 0x2ECFB0);

  // Subtitle
  tft.setTextColor(0xFFFFFF);
  tft.setTextSize(2);
  const char *sub = "Universal Cable Tester";
  int subW = strlen(sub) * 12;
  tft.setCursor((tft.width() - subW) / 2, brandY + 60);
  tft.print(sub);

  tft.setTextColor(0x2ECFB0);
  tft.setTextSize(2);
  const char *ver = "V1.0.0";
  int verW = strlen(ver) * 12;
  tft.setCursor((tft.width() - verW) / 2, brandY + 85);
  tft.print(ver);

  // Password prompt
  tft.setTextColor(0x2ECFB0);
  tft.setTextSize(2);
  const char *prompt = "Enter password:";
  int promptW = strlen(prompt) * 12;
  tft.setCursor((tft.width() - promptW) / 2, brandY + 130);
  tft.print(prompt);

  // Hint text
  tft.setTextColor(0xAAB2BE);
  tft.setTextSize(1);
  const char *hint = "digits then # to confirm  *  to clear";
  int hintW = strlen(hint) * 6;
  tft.setCursor((tft.width() - hintW) / 2, brandY + 210);
  tft.print(hint);

  const int BOX_W = 120, BOX_H = 36;
  const int BOX_X = (tft.width() - BOX_W) / 2;
  const int BOX_Y = brandY + 155;
  const int TEXT_Y = BOX_Y + 10;
  tft.drawRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x2ECFB0);

  char pwInput[5] = "";
  char pwMasked[5] = "";
  bool unlocked = false;
  char lastKey = 0;

  auto redrawBox = [&](bool err) {
    tft.fillRect(BOX_X + 1, BOX_Y + 1, BOX_W - 2, BOX_H - 2, 0x3A2A7A);
    tft.setTextColor(err ? 0xFF0000 : 0xFFFFFF);
    tft.setTextSize(2);
    int tw = strlen(pwMasked) * 12;
    tft.setCursor(BOX_X + (BOX_W - tw) / 2, TEXT_Y);
    tft.print(pwMasked);
  };

  while (!unlocked) {
    char key = i2c_keypad_get_char();
    bool newKey = (key != 'N' && key != lastKey);
    lastKey = (key == 'N') ? 0 : key;

    if (newKey) {
      if (key >= '0' && key <= '9' && strlen(pwInput) < 4) {
        int len = strlen(pwInput);
        pwInput[len] = key;
        pwInput[len + 1] = '\0';
        pwMasked[len] = '*';
        pwMasked[len + 1] = '\0';
        redrawBox(false);
      } else if (key == '*') {
        pwInput[0] = '\0';
        pwMasked[0] = '\0';
        redrawBox(false);
        tft.drawRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x2ECFB0);
      } else if (key == '#') {
        if (strcmp(pwInput, PASSWORD) == 0) {
          tft.drawRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x00FF00);
          buzzBuzzer(300, BUZZER_PASS_FREQ);
          vTaskDelay(pdMS_TO_TICKS(600));
          unlocked = true;
        } else {
          tft.drawRect(BOX_X, BOX_Y, BOX_X, BOX_H, 0xFF0000);
          tft.setTextColor(0xFF0000);
          tft.setTextSize(1);
          int ey = BOX_Y + BOX_H + 6;
          tft.fillRect(0, ey, tft.width(), 14, 0x3A2A7A);
          tft.setCursor((tft.width() - 78) / 2, ey + 2);
          tft.print("Wrong password");
          buzzBuzzer(200, BUZZER_FAIL_FREQ);
          vTaskDelay(pdMS_TO_TICKS(100));
          buzzBuzzer(200, BUZZER_FAIL_FREQ2);
          vTaskDelay(pdMS_TO_TICKS(700));
          pwInput[0] = '\0';
          pwMasked[0] = '\0';
          redrawBox(false);
          tft.drawRect(BOX_X, BOX_Y, BOX_W, BOX_H, 0x2ECFB0);
          tft.fillRect(0, ey, tft.width(), 14, 0x3A2A7A);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
// ============================================================
//                    APP MAIN
// ============================================================
extern "C" void app_main(void) {
  memfault_boot();
  mux_gpio_init();
  adc_init();
  i2c_keypad_init();
  // Keypad test
  printf("Keypad connected: %s\n", i2c_keypad_is_connected() ? "YES" : "NO");
  buzzer_init();
  wifi_init();
  wifi_auto_connect();
  memfault_device_info_dump();

  tft.init();
  tft.setRotation(1);
  ui_init_colours();

  // Splash screen
  // Splash screen
  tft.fillScreen(0x3A2A7A);

  // "plentify" text
  tft.setTextColor(0xFFFFFF);
  tft.setTextSize(4);
  const char *brand = "plentify";
  int brandW = strlen(brand) * 24; // 4 * 6px per char = 24px
  int brandX = (tft.width() - brandW) / 2;
  int brandY = 100;
  tft.setCursor(brandX, brandY);
  tft.print(brand);

  // Teal dot — positioned over the dot of the "i" in "plentify"
  // "plentify" — 'i' is the 6th character (index 5), dot sits ~8px below
  // baseline At textSize 4: each char = 24px wide, baseline at brandY, dot of
  // 'i' ~8px above baseline
  int dotX = brandX + 5 * 24 + 12; // center of the 'i' character
  int dotY = brandY - 8;           // above the baseline where dot of 'i' sits
  tft.fillCircle(dotX, dotY, 7, 0x2ECFB0);

  // Subtitle
  tft.setTextColor(0xFFFFFF);
  tft.setTextSize(2);
  const char *sub = "Universal Cable Tester";
  int subW = strlen(sub) * 12;
  tft.setCursor((tft.width() - subW) / 2, brandY + 60);
  tft.print(sub);

  tft.setTextColor(0x2ECFB0);
  tft.setTextSize(2);
  const char *ver = "V1.0.0";
  int verW = strlen(ver) * 12;
  tft.setCursor((tft.width() - verW) / 2, brandY + 85);
  tft.print(ver);

  buzzBuzzer(300, BUZZER_PASS_FREQ);
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Check keypad
  if (!i2c_keypad_is_connected()) {
    tft.fillScreen(0x3A2A7A);
    tft.setTextColor(0xFF0000);
    tft.setTextSize(2);
    tft.setCursor(60, 140);
    tft.print("Keypad not found!");
    while (true)
      vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Password screen
  runPasswordScreen();

  runMainMenu();
}