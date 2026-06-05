# Universal Sensor & Cable Tester

> A production-grade cable continuity tester built on ESP32-S3, featuring real-time diagnostics, multi-sensor temperature validation, and cloud-based device monitoring via Memfault.

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Framework](https://img.shields.io/badge/framework-ESP--IDF%20v6.0.1-red)
![Language](https://img.shields.io/badge/language-C%2FC%2B%2B-orange)
![Monitoring](https://img.shields.io/badge/monitoring-Memfault-purple)
![License](https://img.shields.io/badge/license-MIT-green)

---

## Overview

The Universal Cable Tester is a standalone embedded device designed to validate sensors and cable assemblies in a manufacturing environment. It tests configurable cable types for continuity faults and short circuits, validates NTC thermistor sensors, and maps unknown pin connections — all through a custom touchscreen-style UI on a 3.5" TFT display.

The project was originally built with PlatformIO and the Arduino framework, then fully migrated to **ESP-IDF** for production use, enabling access to the full ESP32-S3 hardware stack, FreeRTOS task management, and Memfault's device reliability platform.

---

## Features

### Cable Testing
- Tests up to 8 configurable cable harness types
- Voltage-based continuity detection (pass/fail per pair)
- Short circuit detection across all channel combinations
- Real-time results displayed row-by-row with voltage readings
- Pass/fail verdict with audible buzzer feedback

### Temperature Sensor Validation
- **Outlet sensor** — single NTC thermistor ambient delta check
- **Inlet sensor** — dual thermistor (Th1 + Th2) validation
- **Thermostat sensor** — two-phase test: ambient check followed by hot-water immersion rise test
- Steinhart-Hart equation for accurate resistance-to-temperature conversion

### Pin Mapper
- Scans all 32 multiplexed channels automatically
- Displays detected connections in a two-column layout
- Useful for diagnosing unknown or custom cable assemblies

### Device Monitoring (Memfault)
- Crash reporting with coredump saved to dedicated flash partition
- Heartbeat metrics uploaded periodically over WiFi
- OTA firmware update support via Memfault release management
- Automatic reboot reason tracking and device health reporting
- Symbol file upload for fully decoded stack traces

### WiFi Provisioning
- Auto-connects to saved credentials on every boot
- SoftAP captive portal for first-time credential setup
- Phone-based browser UI at `192.168.4.1`
- WiFi status display with IP address
- One-tap credential reset

### User Interface
- 3.5" ILI9486 480×320 TFT display via SPI
- Custom UI built on LovyanGFX — no LVGL overhead
- Branded splash screen and password-protected boot
- Header with live ambient temperature and key input indicator
- Action bar with contextual button labels

---

## Hardware

| Component | Part |
|-----------|------|
| MCU | ESP32-S3-DevKitC-1 |
| Display | 3.5" ILI9486 SPI TFT (480×320) with XPT2046 touch |
| Keypad | 4×4 matrix keypad via PCF8574 I2C GPIO expander |
| Multiplexers | CD74HC4067 16-channel analog MUX ×4 (32 test channels) |
| Temperature sensors | NTC 10kΩ thermistors |
| Buzzer | Passive buzzer via LEDC PWM |
| Flash | 16MB Winbond (on DevKitC-1) |

### Pin Assignments

| Signal | GPIO |
|--------|------|
| TFT MOSI | 11 |
| TFT CLK | 12 |
| TFT CS | 10 |
| TFT DC | 14 |
| TFT RST | 13 |
| TFT MISO | 42 |
| Touch CS | 41 |
| I2C SDA (keypad) | 8 |
| I2C SCL (keypad) | 9 |
| MUX1/2 S0-S3 | 17, 18, 33, 34 |
| MUX1/2 EN1, EN2 | 15, 16 |
| MUX1/2 SIG (output) | 1 |
| MUX3/4 S0-S3 | 35, 36, 37, 38 |
| MUX3/4 EN3, EN4 | 39, 40 |
| MUX3/4 SIG (ADC input) | 2 |
| Ambient temp sensor | 4 |
| Probe temp sensor | 6 |
| Buzzer | 5 |

---

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Framework | ESP-IDF v6.0.1 |
| Language | C / C++ (gnu++26) |
| RTOS | FreeRTOS (built into ESP-IDF) |
| Display driver | LovyanGFX |
| Device monitoring | Memfault SDK v1.39.0 |
| WiFi provisioning | ESP-IDF SoftAP + esp_http_server |
| ADC | ESP-IDF ADC Oneshot driver |
| I2C | ESP-IDF I2C Master driver |
| PWM | ESP-IDF LEDC driver |
| OTA | ESP-IDF esp_https_ota + Memfault releases |

---

## Architecture

```
app_main()
├── memfault_boot()          — crash reporting + metrics init
├── markAppValid()           — OTA rollback guard
├── mux_gpio_init()          — configure 12 MUX control GPIOs
├── adc_init()               — oneshot ADC for 3 channels
├── i2c_keypad_init()        — PCF8574 I2C driver
├── buzzer_init()            — LEDC PWM channel
├── wifi_init()              — STA mode + event handlers
├── wifi_auto_connect()      — restore saved credentials
├── tft.init()               — LovyanGFX SPI display
├── splash screen            — branded boot screen
├── runPasswordScreen()      — PIN entry gate
└── runMainMenu()            — main application loop
    ├── runCableTest(n)      — continuity + short detection
    ├── runSensorTest()
    │   ├── runOutletSensorLoop()
    │   ├── runInletSensorLoop()
    │   └── runThermostatLoop()
    ├── runPinMapper()
    └── runWiFiSettings()
        ├── Auto-connect
        ├── SoftAP portal
        ├── Status display
        └── Credential reset
```

---

## Migration: Arduino → ESP-IDF

This project was originally written for PlatformIO with the Arduino framework using LovyanGFX, WiFiManager, and I2CKeyPad libraries. It was fully migrated to ESP-IDF to enable:

| Arduino | ESP-IDF Replacement |
|---------|-------------------|
| `Arduino.h` / `Wire.h` | `driver/i2c_master.h` |
| `analogRead()` | `esp_adc/adc_oneshot.h` |
| `digitalWrite()` / `pinMode()` | `driver/gpio.h` |
| `ledcSetup()` / `ledcWriteTone()` | `driver/ledc.h` |
| `delay()` / `delayMicroseconds()` | `vTaskDelay()` / `esp_rom_delay_us()` |
| `Serial.printf()` | `printf()` / `ESP_LOGI()` |
| `String` class | `char[]` + `snprintf()` |
| `WiFiManager` | Custom SoftAP + `esp_http_server` |
| `I2CKeyPad` library | Custom PCF8574 row-scan driver |
| `setup()` + `loop()` | `app_main()` with `while(true)` |

LovyanGFX was retained as a local component since it supports ESP-IDF natively, preserving all display code with minimal changes.

---

## Project Structure

```
Universal_Cable_Tester/
├── main/
│   ├── main.cpp              — application entry point and all UI logic
│   ├── user_interface.h      — LovyanGFX config + colour palette + layout
│   ├── mux_control.h         — multiplexer GPIO control
│   ├── temp_sensors.h        — ADC + thermistor + Steinhart-Hart
│   ├── cable_configs.h       — cable mapping definitions
│   ├── i2c_keypad.h          — PCF8574 I2C keypad driver
│   ├── buzzer.h              — LEDC PWM buzzer driver
│   ├── wifi_manager.h        — WiFi STA + SoftAP captive portal
│   └── CMakeLists.txt
├── components/
│   └── LovyanGFX/            — display library (local component)
├── managed_components/
│   └── memfault__memfault-firmware-sdk/
├── partitions.csv            — custom partition table
├── sdkconfig.defaults        — project configuration defaults
├── CMakeLists.txt
└── README.md
```

---

## Getting Started

### Prerequisites

- [ESP-IDF v6.0.1](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/)
- Python 3.11+
- A Memfault account (free) at [app.memfault.com](https://app.memfault.com)

### Configuration

1. Clone the repository:
```bash
git clone https://github.com/YOUR_USERNAME/Universal_Cable_Tester.git
cd Universal_Cable_Tester
```

2. Add your Memfault project key to `sdkconfig.defaults`:
```ini
CONFIG_MEMFAULT_PROJECT_KEY="your_project_key_here"
```

3. Clone LovyanGFX into the components folder:
```bash
mkdir components
cd components
git clone https://github.com/lovyan03/LovyanGFX.git
cd ..
```

### Build and Flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial port (e.g. `COM12` on Windows, `/dev/ttyACM0` on Linux).

### First Boot

1. The device will boot to the splash screen
2. Enter the default PIN: `1234`
3. Go to **WiFi Settings → Start Config Portal**
4. Connect your phone to `ESP32_CableTester`
5. Open a browser and go to `192.168.4.1`
6. Enter your WiFi credentials
7. The device will connect and auto-connect on all future boots

---

## Memfault Integration

### Symbol File Upload

After each build, upload the symbol file to enable decoded crash traces:

```bash
memfault --org YOUR_ORG --project YOUR_PROJECT \
  --org-token YOUR_ORG_TOKEN \
  upload-mcu-symbols build/Universal_Cable_Tester.elf.memfault_log_fmt
```

### OTA Updates

1. Build new firmware with an updated `software_version`
2. Go to **Software → OTA Releases** on app.memfault.com
3. Create a new release and upload `build/Universal_Cable_Tester.bin`
4. Activate the release for your cohort
5. The device will download and install automatically on next check

---

## Cable Configuration

Cable mappings are defined in `cable_configs.h`. Each `Mapping` struct defines:

```cpp
struct Mapping {
    int endA_mux_start;    // MUX start offset for end A
    int endB_mux_start;    // MUX start offset for end B
    int mapA[8];           // pin numbers on end A
    int mapB[8];           // expected corresponding pins on end B
    int length;            // number of pairs to test
    const char *config_name;
};
```

To add a new cable type, add an entry to the `configs[]` array in `main.cpp`.

---

## Serial Output Format

All test results are logged over UART at 115200 baud in CSV format:

```
SESSION,<config_name>,<timestamp_ms>
RESULT,<config>,A<pin>,B<pin>,<voltage>,<PASS|FAIL>
SHORT,<config>,A<pin>,B<pin>,<voltage>
OVERALL,<config>,<PASS|FAIL>,<ambient_temp>
SENSOR,<sensor_name>,<ambient>,<measured>,<delta>,<PASS|FAIL>
MAP_DETECTION,<pin_a>-><pin_b>
```

---

## Enclosure
_ **Onshape ENclosure Link: https://cad.onshape.com/documents/77dd7369e92edf708e8c193e/w/8bffdc50b152b12f82e31c56/e/991ae3f50e2370966ed71790

## What I Learned

- **Full ESP-IDF migration** from Arduino framework including custom peripheral drivers
- **PCF8574 I2C keypad scanning** — reverse-engineered row/column scanning from raw hardware readings
- **LovyanGFX with ILI9486** — rgb888 colour pipeline quirks over SPI (16bpp not supported by ILI9486 over SPI)
- **Memfault SDK integration** for ESP-IDF v6.0.1 including compatibility fixes for renamed driver headers
- **SoftAP captive portal** from scratch using `esp_http_server` with URL decoding
- **ADC channel mapping** on ESP32-S3 (GPIO to ADC channel mapping is non-trivial)
- **Partition table design** for coredump + OTA dual-bank layout

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

## Author

## Author

**Enock Kashama** — Embedded Software Engineer passionate about 
reliable firmware, IoT connectivity and production-grade device monitoring.

- 🔗 [GitHub](https://github.com/EnockKashama)
- 💼 [LinkedIn](https://www.linkedin.com/in/enockkabamba/)
- 📧 henockkashama94@gmail.com
