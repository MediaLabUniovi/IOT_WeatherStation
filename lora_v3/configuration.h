#pragma once

#define     LOOP_TIME             20000
#define     TX_BUFFER_SIZE        20
#define     WAKE_TIME             40000

#define     BatteryPin            12
#define     VoltagePin            0

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Wire.h>

Adafruit_BME280 bme;

#define     WindVanePin           4

long rotations = 0;
long ContactBounceTime=0;
#define     windSensorPin         34
#define     rainSensorPin         36
#define     rainInterval          1000
volatile long tipTime = millis();
volatile long rainTime = 0;
volatile int OverflowCount = 0;


#define SEALEVELPRESSURE_HPA (1013.25)

const lmic_pinmap lmic_pins = {
    .nss = 18, 
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 23,
    .dio = {/*dio0*/ 26, /*dio1*/ 33, /*dio2*/ 32} 
};
