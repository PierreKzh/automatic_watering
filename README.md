# automatic_watering

## Objective
- Operate a water pump at a specific time every day for a certain period of time.

## Features
- Change the watering time
- Change the watering duration
- Manually activate the pump
- Green LED illuminates when the system is running
- Flashing orange LED when the pump is activated
- A menu selection button
- A selection button

## Hardware
### Electronics
- Arduino Nano
- 20x4 I2C LCD display
- 2 buttons
- 2 LEDs + 2 300Ω resistors
- DS3231 - i2C RTC
- Relay

### Electrical
> My pump operates at 12V 3A but can have a peak at 20A at startup.
- Two parallel 12V 10A relays operating at 12V.
- LM2596S - 12V to 5V to power the Arduino.
- 230V to 12V 20A power supply.
- 12V 3A pump (not connected to the relays in the following photos).

## Photos
![image1](https://github.com/PierreKzh/automatic_watering/blob/main/pictures/IMG_20250406_142111.jpg)
![image2](https://github.com/PierreKzh/automatic_watering/blob/main/pictures/IMG_20250406_142214.jpg)
![image3](https://github.com/PierreKzh/automatic_watering/blob/main/pictures/IMG_20250406_142303.jpg)
![image4](https://github.com/PierreKzh/automatic_watering/blob/main/pictures/IMG_20250406_142601.jpg)
