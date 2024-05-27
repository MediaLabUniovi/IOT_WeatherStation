Proyecto de Fin de Grado de Miguel Moure Prado

La estacion meteorológica cumple los siguientes requisitos: 
  1) Medir direccion del viento (Puntos cardinales y Grados)
  2) Medir la velocidad del viento (km/h)
  3) Medir precipitación (mm/h)
  4) Medir la temperatura (°C) Humedad relativa (%) y presión (hPa)
  5) Utilizar Red Lora

Se desarrolla un proyecto en 5 fases: 
  1) Sensórica
  2) Microcontrolador
  3) Alimentación
  4) Comunicaciones
  5) Visualizacion



![DC-DC](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/0809f150-b4f7-43c1-aee0-568f5c1f5307)


![Veleta_schematic](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/1d68a2b7-fcc6-4aea-b5f0-ea28ee5b7db8)



1ª FASE: Sensorica 
  Direccion del viento: https://www.davisinstruments.com/products/wind-vane-d-shaped-receptacle
  
    Veleta que consta con un potenciometro de resistencia variable (0-20 kΩ) que nos proporciona un           voltaje   que recogemos en un pin del microcontrolador conectado al      conversor analógico/digital.

  
  ![Uploading Veleta_schematic.png…]()

     

  Velocidad del viento: https://www.davisinstruments.com/products/anemometer-for-vantage-pro2-vantage-pro
  
    Las copas del anemometro hacen girar un conjunto de eje y rodamiento, que a su vez hacen girar un         imán que una vez por vuelta pasa sobre un rele reed, cerrando sus contactos y generando un pulso          descendente que recogemos en un pin del microcontrolador
    
    
  ![Anemometro Schematic](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/5346ba67-f1c6-4fd0-a123-2bbc977e502c)

  Precipitación: https://www.davisinstruments.com/products/aerocone-rain-collector-with-flat-base-for-vantage-pro2
  
    La lluvia cae en el cono del pluviómetro y se deposita en una cubeta, cuando esta se llena, cae por       su propio peso cerrando el contacto de un rele reed y generando un pulso ascendente que recogemos en      un pin del microcontrolador. 


    
![Pluviometro Schematic](https://github.com/MediaLabUniovi/WeatherStation/assets/159242374/17ed912b-a580-4c68-ac1c-ba81a2e0a0ed)

   

  Temperatura, humedad y presion
