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

// =========================
// SHT85
// =========================
constexpr uint8_t SHT_ADDRESS = 0x44;

// =========================
// Sensores
// =========================
constexpr uint32_t WIND_DEBOUNCE_MS = 15;
constexpr uint32_t RAIN_IGNORE_MS = 2000;
constexpr float RAIN_MM_PER_TIP = 0.2f;
constexpr float WIND_FACTOR = 0.9f;
constexpr float MPH_TO_KMH = 1.61f;

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

// =========================
// Prototipos
// =========================
void IRAM_ATTR onWindInterrupt();
void IRAM_ATTR onRainInterrupt();

void sleepMillis(uint64_t ms);
uint64_t getPendingSleepMs();

void initSensors();

float readBatteryVoltage();
float readWindDirection();
float readWindSpeedKmh();
float readRainRateMmH();

int windDirection();
float windSpeed();
void rotate();
float rainRate();
void rain();
uint32_t getRainTipsAccumulated();
float getRainAccumulatedMm();

void readSensors(SensorPayload& payload);
size_t get_payload(const SensorPayload& values, uint8_t* out, size_t out_size);

void doSend();
void onEvent(ev_t ev);
void printHex2(unsigned v);
