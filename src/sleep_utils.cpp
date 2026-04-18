#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "config.h"

RTC_DATA_ATTR uint64_t g_sleepDeadlineUs = 0;

void sleepMillis(uint64_t ms) {
    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    g_sleepDeadlineUs = nowUs + (ms * 1000ULL);

    esp_sleep_enable_timer_wakeup(ms * 1000ULL);

    if (ENABLE_RAIN_SENSOR) {
        // ext0 es por nivel: si el pin entra en sleep ya en HIGH, despierta en bucle.
        // Con pull-down externo, el reposo correcto es LOW.
        pinMode(RAIN_SENSOR_PIN, INPUT);
        int rainPinState = digitalRead(RAIN_SENSOR_PIN);

        if (rainPinState == LOW) {
            // RAIN_SENSOR_PIN=36 es RTC GPIO en ESP32, permite wakeup desde deep sleep.
            esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(RAIN_SENSOR_PIN), 1);
        } else {
            Serial.println("[Rain] Pin lluvia en HIGH al dormir; wake por lluvia desactivado en este ciclo");
        }
    }

    esp_deep_sleep_start();
}

uint64_t getPendingSleepMs() {
    if (g_sleepDeadlineUs == 0) {
        return 0;
    }

    const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
    if (nowUs >= g_sleepDeadlineUs) {
        g_sleepDeadlineUs = 0;
        return 0;
    }

    return (g_sleepDeadlineUs - nowUs) / 1000ULL;
}