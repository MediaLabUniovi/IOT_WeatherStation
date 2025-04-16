#include "configuration.h"

float bmeTemp() {
  // Obtenemos y printeamos la temperatura
  float temp = bme.readTemperature();
  Serial.print("Temperature = ");
  Serial.print(temp);
  Serial.println(" °C");
  return temp;
}

float bmePres() {
  // Obtenemos y printeamos la presion en hpa
  float pressure = bme.readPressure();
  Serial.print("Pressure = ");
  Serial.print(pressure / 100.0F);
  Serial.println(" hPa");
  return pressure;
}

float bmeAlt() {
  // Obtenemos y printeamos la altitud en metros
  float altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
  float height = altitude + 56;
  Serial.print("Approx. Altitude = ");
  Serial.print(height);
  Serial.println(" m");
  return height;
}

float bmeHum() {
  // Obtenemos y printeamos el porcentaje de humedad
  float hum = bme.readHumidity();
  Serial.print("Humidity = ");
  Serial.print(hum);
  Serial.println(" %");
  return hum;
}

int windDirection() {
  int vaneValue = analogRead(WindVanePin);
  int windDirection = map(vaneValue, 0, 3050, 0, 360);
  int windCalDirection = windDirection;
  if (windCalDirection > 360) windCalDirection = windCalDirection - 360;
  if (windCalDirection < 0) windCalDirection = windCalDirection + 360;

  String windCompassDirection = " ";
  if (windCalDirection < 22) windCompassDirection = "N";
  else if (windCalDirection < 67) windCompassDirection = "NE";
  else if (windCalDirection < 112) windCompassDirection = "E";
  else if (windCalDirection < 157) windCompassDirection = "SE";
  else if (windCalDirection < 212) windCompassDirection = "S";
  else if (windCalDirection < 247) windCompassDirection = "SW";
  else if (windCalDirection < 292) windCompassDirection = "W";
  else if (windCalDirection < 337) windCompassDirection = "NW";
  else windCompassDirection = "N";


  Serial.print("Wind direction: ");
  Serial.print(windCalDirection);
  Serial.println(windCompassDirection);
  return windCalDirection;
}

float windSpeed(){
  float windSpeed = rotations * 0.9;
  rotations = 0;
  windSpeed = windSpeed * 1.61;
  Serial.print("Velocidad del viento =");
  Serial.print(windSpeed);
  Serial.println("km/h");
  return windSpeed;
}

void rotate() {
  if ((millis() - ContactBounceTime) > 15) {
    rotations++;
    ContactBounceTime = millis();
  }
}

float rainRate() {
  
  float precipitacion = (0.2 * 3600000) / rainTime;
  Serial.print("Rainrate");
  Serial.print(precipitacion);
  
  return precipitacion;
}

void rain() {
  Serial.println("RainCount");
  long currentTime = millis();
  if ((currentTime - tipTime) < 2000) {
    return;
  }
  
  if (OverflowCount == 0) {
    rainTime = 100000; // TODO DEFINE LONG NUMBRE TO GET 0 RAIN
    OverflowCount = 1;
  } else {
    rainTime = currentTime - tipTime;
  }

  
  tipTime = currentTime;
}

int ReadBattery() {
  int analogBat = analogRead(BatteryPin);
  Serial.print(analogBat);
  float digitalBat = (analogBat - 0) * (2.1 - 0) / (2500 - 0);
  int battery = digitalBat * 2;
  Serial.print(" Bateria Real ");
  return battery;
}

void doSensor(uint8_t txBuffer[]) {
  // Llenar el búfer con caracteres nulos para borrar el contenido anterior
  memset(txBuffer, 0, TX_BUFFER_SIZE);

  int battery = ReadBattery();
  sprintf((char*)txBuffer, "B:%d", battery);

  float t = bmeTemp();
  int shiftTemp = int(t * 100);
  txBuffer[0] = byte(shiftTemp);
  txBuffer[1] = shiftTemp >> 8;


  float p = bmePres();
  int shiftpresion = int(p * 100);
  txBuffer[2] = byte(shiftpresion);
  txBuffer[3] = shiftpresion >> 8;
  txBuffer[4] = shiftpresion >> 16;
  txBuffer[5] = shiftpresion >> 32;


  float a = bmeAlt();
  int shiftAltura = int(a * 100);
  txBuffer[6] = byte(shiftAltura);
  txBuffer[7] = shiftAltura >> 8;


  float h = bmeHum();
  int shifthumedad = int(h * 100);
  txBuffer[8] = byte(shifthumedad);
  txBuffer[9] = shifthumedad >> 8;

  float d = windDirection();
  int shiftdirviento = int(d * 100);
  txBuffer[10] = byte(shiftdirviento);
  txBuffer[11] = shiftdirviento >> 8;
  txBuffer[12] = shiftdirviento >> 16;

  float v = windSpeed();
  int shiftVel = int(v * 100);
  txBuffer[13] = byte(shiftVel);
  txBuffer[14] = shiftVel >> 8;


  float r = rainRate();
  int shiftprecipitacion = int(r * 100);
  txBuffer[15] = byte(shiftprecipitacion);
  txBuffer[16] = shiftprecipitacion >> 8;
  txBuffer[17] = shiftprecipitacion >> 16;

  int shiftVoltage = int(battery * 10);
  txBuffer[18] = byte(shiftVoltage);
}
