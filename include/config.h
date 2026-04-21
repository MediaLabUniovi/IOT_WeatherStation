#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SHT85.h>

// =========================
// Configuracion general
// =========================
constexpr uint32_t LOOP_TIME_MS = 20000;
constexpr uint32_t WAKE_TIME_MS = 5 * 60 * 1000; // 5 minutos
constexpr uint8_t LORA_FPORT = 1;
constexpr bool ENABLE_DEEP_SLEEP = true;
constexpr bool ENABLE_LORA = true;

// =========================
// Sensores habilitados
// =========================
constexpr bool ENABLE_SHT85 = true;
constexpr bool ENABLE_BATTERY = true;
constexpr bool ENABLE_WIND_SENSOR = true;
constexpr bool ENABLE_RAIN_SENSOR = true;
constexpr bool ENABLE_WIND_VANE = true;
constexpr bool ENABLE_PRESSURE = true;
constexpr bool ENABLE_ALTITUDE = true;

// =========================
// Pines
// =========================
constexpr uint8_t BATTERY_PIN = 12;
constexpr uint8_t VOLTAGE_PIN = 15;
constexpr uint8_t WIND_VANE_PIN = 4;
constexpr uint8_t WIND_SENSOR_PIN = 34;
constexpr uint8_t RAIN_SENSOR_PIN = 36;

// GPIO 34..39 en ESP32 no tienen pull-up/pull-down internos.
// Si no hay resistencias externas, las interrupciones pueden generar ruido
// continuo y provocar reinicios por watchdog (INT_WDT).
constexpr bool WIND_SENSOR_HAS_EXTERNAL_BIAS = false;
constexpr bool RAIN_SENSOR_HAS_EXTERNAL_BIAS = false;

// =========================
// I2C (SHT85)
// =========================
constexpr uint8_t SHT_SDA_PIN = 21;
constexpr uint8_t SHT_SCL_PIN = 22;
constexpr uint32_t SHT_I2C_CLOCK_HZ = 100000;
constexpr uint8_t SHT_INIT_RETRIES = 3;
constexpr bool SHT_TRY_DEFAULT_WIRE_FIRST = true;

// =========================
// SHT85
// =========================
constexpr uint8_t SHT_ADDRESS = 0x44;

// =========================
// Sensores
// =========================
constexpr uint32_t WIND_DEBOUNCE_MS = 15;
constexpr uint32_t RAIN_IGNORE_MS = 250;
constexpr float RAIN_MM_PER_TIP = 0.2f;
constexpr uint32_t RAIN_RATE_AVG_WINDOW_MS = 10 * 60 * 1000;  // 10 min
constexpr uint8_t RAIN_TIP_HISTORY_SIZE = 64;
constexpr float WIND_FACTOR = 0.9f;
constexpr float MPH_TO_KMH = 1.61f;

// =========================
// Bateria (ADC)
// =========================
constexpr uint8_t BATTERY_ADC_SAMPLES = 32;
constexpr float BATTERY_ADC_MAX_READING = 4095.0f;
constexpr float BATTERY_ADC_VREF = 3.3f;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float BATTERY_CALIBRATION_FACTOR = 1.0f;
constexpr float BATTERY_CALIBRATION_OFFSET_V = 0.30f;

// =========================
// LMIC pinmap - LILYGO T3 v1.6.1
// =========================
extern const lmic_pinmap lmic_pins;

// =========================
// Payload legacy (igual al original)
// =========================
constexpr size_t LORAWAN_PAYLOAD_LEN = 23U;

struct SensorPayload {
    int16_t temperature_c_x100;      // bytes 0..1
    int32_t pressure_x100;           // bytes 2..5
    int16_t altitude_m_x100;         // bytes 6..7
    int16_t humidity_pct_x100;       // bytes 8..9
    int32_t wind_dir_deg_x100;       // bytes 10..12 (3 bytes LSB)
    int16_t wind_speed_kmh_x100;     // bytes 13..14
    int32_t rain_rate_x10;           // bytes 15..17 (3 bytes LSB)
    int32_t rain_accum_x10;          // bytes 18..21 (lluvia acumulada 24h x10)
    uint8_t battery_x10;             // byte 22
};

static_assert(LORAWAN_PAYLOAD_LEN <= 51, "Payload LoRaWAN demasiado grande");

// =========================
// Objetos globales
// =========================
extern SHT85 sht;

// =========================
// Variables de interrupcion
// =========================
extern volatile uint32_t windRotations;
extern volatile uint32_t lastWindInterruptMs;
extern volatile uint32_t lastRainTipMs;
extern volatile uint32_t rainIntervalMs;
extern volatile bool hasRainSample;
extern bool g_debugDisplayMode;

// =========================
// Prototipos
// =========================
void IRAM_ATTR onWindInterrupt();
void IRAM_ATTR onRainInterrupt();

void sleepMillis(uint64_t ms);
uint64_t getPendingSleepMs();

void initSensors();

float readBatteryVoltage();

int windDirection();
float windSpeed();
void rotate();
float rainRate();
bool rain();
uint32_t processRainInterrupts();
uint32_t getRainTipsAccumulated();
float getRainAccumulatedMm();

void readSensors(SensorPayload& payload, bool printTable = true);
size_t get_payload(const SensorPayload& values, uint8_t* out, size_t out_size);

void doSend();
void onEvent(ev_t ev);
void printHex2(unsigned v);
