1ª FASE: Sensorica 

  Direccion del viento: https://www.davisinstruments.com/products/wind-vane-d-shaped-receptacle
  
  Veleta que consta con un potenciometro de resistencia variable (0-20 kΩ) que nos proporciona un voltaje que recogemos en un pin del microcontrolador conectado al 
  conversor analógico/digital.


![Veleta_schematic](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/de57982b-7076-4cde-a660-335a309e646f)


     

  Velocidad del viento: https://www.davisinstruments.com/products/anemometer-for-vantage-pro2-vantage-pro
  
  Las copas del anemometro hacen girar un conjunto de eje y rodamiento, que a su vez hacen girar un imán que una vez por vuelta pasa sobre un rele reed, cerrando sus contactos y generando un pulso          descendente que recogemos en un pin del microcontrolador
    
 ![Anemometro Schematic](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/f25c58d4-cc9a-449d-ab5a-3490521289f2)
   
  
  Precipitación: https://www.davisinstruments.com/products/aerocone-rain-collector-with-flat-base-for-vantage-pro2
  
  La lluvia cae en el cono del pluviómetro y se deposita en una cubeta, cuando esta se llena, cae por su propio peso cerrando el contacto de un rele reed y generando un pulso ascendente que recogemos en      un pin del microcontrolador. 


    
![Pluviometro Schematic](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/7625b989-4968-402c-82ac-e03e56dfb308)


Temperatura, Humedad y Presión: https://www.amazon.es/AZDelivery-GY-BME280-Sensor-Parent/dp/B07D8T4HP6

  Utilizamos el sensor BME280 para leer estos valores. Mediante la libreria "Adafruit_BME280" que utiliza funciones que siguen el protocolo I2C accedemos a los registros que guardan estos valores 

  ![BME](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/6bb0c832-5b1d-417c-b621-b2cb0a0b4f2b)

